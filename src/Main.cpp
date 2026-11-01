#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include "Core.hpp"
#include "Library.hpp"
#include "Transfer.hpp"
#include <functional>
#include <limits>
#include <thread>
using namespace geode::prelude;
namespace {
void alert(std::string const& text) {
    if (!text.empty()) FLAlertLayer::create("Menu Music", text.c_str(), "OK")->show();
}
CCLabelBMFont* label(CCNode* parent, std::string const& text, CCPoint pos,
                     float scale = .4f, float width = 360.f) {
    auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    l->setPosition(pos);
    l->limitLabelWidth(width, scale, .15f);
    parent->addChild(l);
    return l;
}
CCMenuItemSpriteExtra* button(CCMenu* menu, char const* text, CCObject* target,
                              SEL_MenuHandler callback, CCPoint pos,
                              float width = 95.f, float scale = .5f) {
    auto s = ButtonSprite::create(text);
    s->setScale(std::min(scale, width / s->getContentSize().width));
    auto b = CCMenuItemSpriteExtra::create(s, target, callback);
    b->setPosition(pos);
    menu->addChild(b);
    return b;
}
CCMenu* menuAtOrigin(CCNode* parent) {
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    parent->addChild(menu, 5);
    return menu;
}
class AddPopup final : public Popup {
    TextInput* m_url = nullptr;
    TextInput* m_name = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_fileButton = nullptr;
    CCMenuItemSpriteExtra* m_downloadButton = nullptr;
    async::TaskHolder<file::PickResult> m_picker;
    std::shared_ptr<mm::JobState> m_job = std::make_shared<mm::JobState>();
    bool m_busy = false;

    void setBusy(bool busy) {
        m_busy = busy;
        m_fileButton->setEnabled(!busy);
        m_downloadButton->setEnabled(!busy);
        m_url->setEnabled(!busy);
        m_name->setEnabled(!busy);
    }
    void status(std::string const& text) {
        m_status->setString(text.c_str());
        m_status->limitLabelWidth(370, .28f, .15f);
    }
    void tick(float) {
        if (m_busy) status(fmt::format("Working: {:.1f} MiB / max 64 MiB. Close to cancel.",
                                      m_job->bytes.load() / 1048576.0));
    }
    void begin(bool download, std::string url, std::filesystem::path source, std::string title) {
        if (m_busy) return;
        try {
            auto temporary = mm::Library::get().temporaryPath();
            setBusy(true);
            m_job->bytes = 0;
            m_job->cancel = false;
            auto state = m_job;
            // The worker owns only plain data. It never touches Cocos or FMOD.
            std::thread([this, state, temporary, download, url = std::move(url),
                         source = std::move(source), title = std::move(title)]() mutable {
                auto result = download ? mm::downloadAudio(url, temporary, state)
                                       : mm::copyAudio(source, temporary, state);
                geode::queueInMainThread([this, state, result = std::move(result), title = std::move(title)]() mutable {
                    if (!state->alive.load()) {
                        std::error_code ec;
                        if (!result.file.empty()) std::filesystem::remove(result.file, ec);
                        return;
                    }
                    setBusy(false);
                    if (!result.error.empty()) { status("Import failed. You can try again."); alert(result.error); return; }
                    auto error = mm::Library::get().importTemporary(result.file, std::move(title));
                    if (!error.empty()) { status("Import failed. You can try again."); alert(error); return; }
                    this->onClose(nullptr);
                });
            }).detach();
        } catch (std::exception const& e) {
            setBusy(false);
            status("Could not start the import.");
            alert(e.what());
        }
    }
    void onFile(CCObject*) {
        if (m_busy || m_picker.isPending()) return;
        file::FilePickOptions options;
        options.filters.push_back({"Audio (MP3, OGG, WAV, FLAC)", {"*.mp3", "*.ogg", "*.wav", "*.flac"}});
        m_fileButton->setEnabled(false);
        m_downloadButton->setEnabled(false);
        status("Choose an audio file in the Windows dialog.");
        m_picker.spawn(file::pick(file::PickMode::OpenFile, options), [this](file::PickResult result) {
            m_fileButton->setEnabled(true);
            m_downloadButton->setEnabled(true);
            if (result.isErr()) { status("File dialog failed."); alert("Could not open the Windows file picker."); return; }
            auto picked = std::move(result).unwrap();
            if (!picked) { status("No file selected."); return; }
            auto title = mm::trim(std::string(m_name->getString().c_str()));
            if (title.empty()) title = mm::utf8(picked->stem());
            begin(false, {}, *picked, std::move(title));
        });
    }
    void onDownload(CCObject*) {
        if (m_busy || m_picker.isPending()) return;
        auto url = mm::trim(std::string(m_url->getString().c_str()));
        if (!mm::isHttps(url)) { alert("Paste a direct https:// link to an audio file. YouTube/Spotify pages are not audio files."); return; }
        auto title = mm::trim(std::string(m_name->getString().c_str()));
        if (title.empty()) {
            auto clean = url.substr(0, url.find_first_of("?#"));
            auto slash = clean.rfind('/');
            title = slash != clean.npos ? clean.substr(slash + 1) : "Downloaded track";
            if (title.empty()) title = "Downloaded track";
        }
        begin(true, std::move(url), {}, std::move(title));
    }
    bool init() {
        if (!Popup::init(400, 240)) return false;
        setTitle("Add song");
        label(m_mainLayer, "Direct audio links, not YouTube / Spotify pages", {200, 190}, .27f, 370);
        m_name = TextInput::create(350, "Song title (optional)");
        m_name->setPosition({200, 153});
        m_name->setCommonFilter(CommonFilter::Any);
        m_name->setMaxCharCount(120);
        m_mainLayer->addChild(m_name);
        m_url = TextInput::create(350, "https://example.com/song.mp3");
        m_url->setPosition({200, 104});
        m_url->setCommonFilter(CommonFilter::Any);
        m_url->setMaxCharCount(2048);
        m_mainLayer->addChild(m_url);
        auto menu = menuAtOrigin(m_mainLayer);
        m_fileButton = button(menu, "From file", this, menu_selector(AddPopup::onFile), {110, 58}, 145);
        m_downloadButton = button(menu, "Download", this, menu_selector(AddPopup::onDownload), {290, 58}, 145);
        m_status = label(m_mainLayer, "MP3 / OGG / WAV / FLAC. Up to 64 MiB per song.", {200, 23}, .28f, 370);
        schedule(schedule_selector(AddPopup::tick), .15f);
        return true;
    }
    void onClose(CCObject* sender) override {
        m_picker.cancel();
        m_job->cancel = true;
        m_job->alive = false;
        Popup::onClose(sender);
    }
    ~AddPopup() override { m_job->alive = false; m_job->cancel = true; }
public:
    static AddPopup* create() {
        auto p = new AddPopup;
        if (p->init()) { p->autorelease(); return p; }
        delete p; return nullptr;
    }
};
class MusicPopup final : public Popup {
    static constexpr int PerPage = 4;
    CCLabelBMFont* m_now = nullptr;
    CCLabelBMFont* m_pageLabel = nullptr;
    CCMenu* m_rows = nullptr;
    CCLabelBMFont* m_empty = nullptr;
    int m_page = 0;
    std::size_t m_seen = std::numeric_limits<std::size_t>::max();
    std::shared_ptr<std::atomic_bool> m_alive = std::make_shared<std::atomic_bool>(true);
    void refresh() {
        auto& lib = mm::Library::get();
        m_seen = lib.revision();
        auto const& tracks = lib.tracks();
        int pages = std::max(1, (static_cast<int>(tracks.size()) + PerPage - 1) / PerPage);
        m_page = std::clamp(m_page, 0, pages - 1);
        m_now->setString(lib.currentTitle().c_str());
        m_now->limitLabelWidth(385, .47f, .15f);
        m_pageLabel->setString(fmt::format("{} / {}  ({} songs)", m_page + 1, pages, tracks.size()).c_str());
        m_pageLabel->limitLabelWidth(230, .3f, .15f);
        m_rows->removeAllChildrenWithCleanup(true);
        m_empty->setVisible(tracks.empty());
        for (int row = 0; row < PerPage; ++row) {
            int i = m_page * PerPage + row;
            if (i >= static_cast<int>(tracks.size())) break;
            auto text = fmt::format("{}{}: {}", i == lib.current() ? "> " : "", i + 1, mm::displayName(tracks[i].title));
            auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            l->limitLabelWidth(335, .32f, .12f);
            auto b = CCMenuItemSpriteExtra::create(l, this, menu_selector(MusicPopup::onSelect));
            b->setTag(i);
            b->setPosition({205, 143.f - row * 22.f});
            m_rows->addChild(b);
            auto remove = button(m_rows, "X", this, menu_selector(MusicPopup::onRemove),
                                 {393, 143.f - row * 22.f}, 24, .35f);
            remove->setTag(i);
        }
    }
    void tick(float) { if (m_seen != mm::Library::get().revision()) refresh(); }
    void onPrev(CCObject*) { alert(mm::Library::get().step(false)); refresh(); }
    void onNext(CCObject*) { alert(mm::Library::get().step(true)); refresh(); }
    void onRandom(CCObject*) { alert(mm::Library::get().shuffle()); refresh(); }
    void onDefault(CCObject*) { alert(mm::Library::get().select(-1)); refresh(); }
    void onAdd(CCObject*) { if (auto p = AddPopup::create()) p->show(); }
    void onPagePrev(CCObject*) { --m_page; refresh(); }
    void onPageNext(CCObject*) { ++m_page; refresh(); }
    void onSelect(CCObject* sender) { alert(mm::Library::get().select(sender->getTag())); refresh(); }
    void onRemove(CCObject* sender) {
        auto const& tracks = mm::Library::get().tracks();
        int i = sender->getTag();
        if (i < 0 || i >= static_cast<int>(tracks.size())) return;
        auto filename = tracks[i].file;
        auto alive = m_alive;
        createQuickPopup("Remove song?", "Remove this song from the playlist? Your original file will not be deleted.",
                         "Cancel", "Remove", [this, alive, filename](FLAlertLayer*, bool yes) {
            if (!yes || !alive->load()) return;
            alert(mm::Library::get().remove(filename));
            refresh();
        });
    }
    bool init() {
        if (!Popup::init(430, 290)) return false;
        setTitle("Menu Music Player");
        label(m_mainLayer, "NOW PLAYING", {215, 244}, .3f)->setColor({255, 220, 100});
        m_now = label(m_mainLayer, "Default menu music", {215, 224}, .47f, 385);
        auto menu = menuAtOrigin(m_mainLayer);
        button(menu, "Previous", this, menu_selector(MusicPopup::onPrev), {92, 193}, 110);
        button(menu, "Random", this, menu_selector(MusicPopup::onRandom), {215, 193}, 110);
        button(menu, "Next", this, menu_selector(MusicPopup::onNext), {338, 193}, 110);
        label(m_mainLayer, "PLAYLIST - click a song to play", {215, 164}, .27f);
        m_rows = menuAtOrigin(m_mainLayer);
        m_empty = label(m_mainLayer, "No songs yet. Press Add song.", {215, 112}, .35f);
        button(menu, "<", this, menu_selector(MusicPopup::onPagePrev), {70, 49}, 30, .4f);
        m_pageLabel = label(m_mainLayer, "1 / 1", {215, 49}, .3f, 230);
        button(menu, ">", this, menu_selector(MusicPopup::onPageNext), {360, 49}, 30, .4f);
        button(menu, "Add song", this, menu_selector(MusicPopup::onAdd), {140, 22}, 125, .45f);
        button(menu, "Default", this, menu_selector(MusicPopup::onDefault), {290, 22}, 125, .45f);
        refresh();
        schedule(schedule_selector(MusicPopup::tick), .25f);
        return true;
    }
    void onClose(CCObject* sender) override {
        *m_alive = false;
        Popup::onClose(sender);
    }
    ~MusicPopup() override { *m_alive = false; }
public:
    static MusicPopup* create() {
        auto p = new MusicPopup;
        if (p->init()) { p->autorelease(); return p; }
        delete p; return nullptr;
    }
};
}
// Only replace the game's menu music lookup. Do not hook level song loading,
// overwrite menuLoop.mp3, or create an independent channel that leaks into levels.
class $modify(MenuMusicGameManager, GameManager) {
    gd::string getMenuMusicFile() {
        auto path = mm::Library::get().selectedPath();
        return path.empty() ? GameManager::getMenuMusicFile() : gd::string(path.c_str());
    }
};
class $modify(MenuMusicMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto menu = typeinfo_cast<CCMenu*>(getChildByID("bottom-menu"));
        auto s = ButtonSprite::create("MUSIC");
        s->setScale(.45f);
        auto b = CCMenuItemSpriteExtra::create(s, this, menu_selector(MenuMusicMenuLayer::onMusic));
        b->setID("menu-music-button"_spr);
        if (menu) {
            menu->addChild(b);
            menu->updateLayout();
        } else {
            menu = menuAtOrigin(this);
            auto size = CCDirector::sharedDirector()->getWinSize();
            b->setPosition({size.width - 45.f, 75.f});
            menu->addChild(b);
        }
        return true;
    }
    void onMusic(CCObject*) { if (auto p = MusicPopup::create()) p->show(); }
};
