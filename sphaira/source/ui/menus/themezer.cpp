#include "ui/menus/themezer.hpp"
#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"

#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "fs.hpp"
#include "download.hpp"
#include "ui/nvg_util.hpp"
#include "swkbd.hpp"
#include "i18n.hpp"
#include "threaded_file_transfer.hpp"
#include "image.hpp"

#include <minIni.h>
#include <stb_image.h>
#include <cstring>
#include <yyjson.h>
#include "yyjson_helper.hpp"

namespace sphaira::ui::menu::themezer {
namespace {

// format is /themes/sphaira/Theme Name by Author/theme_name-type.nxtheme
constexpr fs::FsPath THEME_FOLDER{"/themes/sphaira/"};
constexpr auto CACHE_PATH = "/switch/sphaira/cache/themezer";
constexpr auto URL_BASE = "https://switch.cdn.fortheusers.org";

constexpr const char* REQUEST_TARGET[]{
    "ResidentMenu",
    "Entrance", 
    "Flaunch",
    "Set",
    "Psl",
    "MyPage",
    "Notification"
};

constexpr const char* REQUEST_SORT[]{
    "DOWNLOADS",
    "UPDATED", 
    "SAVES",
    "CREATED"
};

constexpr const char* REQUEST_ORDER[]{
    "DESC",
    "ASC"
};

// New GraphQL API endpoints:
// Themes: https://api.themezer.net/graphql?query=query($target:Target,$page:PositiveInt,$limit:PositiveInt,$sort:ItemSort,$order:SortOrder,$query:String,$includeNSFW:Boolean){switchThemes(target:$target,page:$page,limit:$limit,sort:$sort,order:$order,query:$query,includeNSFW:$includeNSFW){nodes{hexId,creator{username},name,description,updatedAt,downloadCount,saveCount,target,previewJpgLargeUrl,previewJpgSmallUrl,downloadUrl}pageInfo{itemCount,limit,page,pageCount}}}
// Packs: https://api.themezer.net/graphql?query=query($page:PositiveInt,$limit:PositiveInt,$sort:ItemSort,$order:SortOrder,$query:String,$includeNSFW:Boolean){switchPacks(page:$page,limit:$limit,sort:$sort,order:$order,query:$query,includeNSFW:$includeNSFW){nodes{hexId,creator{username},name,description,updatedAt,downloadCount,saveCount,previewJpgLargeUrl,previewJpgSmallUrl,themes{hexId,creator{username},name,description,updatedAt,downloadCount,saveCount,target,previewJpgLargeUrl,previewJpgSmallUrl,downloadUrl}}pageInfo{itemCount,limit,page,pageCount}}}

// Updated for new GraphQL API
auto apiBuildUrlListInternal(const Config& e, bool is_pack) -> std::string {
    std::string base_url = "https://api.themezer.net/graphql";
    std::string query;
    std::string variables;
    
    if (is_pack) {
        // GraphQL query for packs with NSFW support
        query = "query%28%24page%3APositiveInt%2C%24limit%3APositiveInt%2C%24sort%3AItemSort%2C%24order%3ASortOrder%2C%24query%3AString%2C%24includeNSFW%3ABoolean%29%7BswitchPacks%28page%3A%24page%2Climit%3A%24limit%2Csort%3A%24sort%2Corder%3A%24order%2Cquery%3A%24query%2CincludeNSFW%3A%24includeNSFW%29%7Bnodes%7BhexId%2Ccreator%7Busername%7D%2Cname%2Cdescription%2CupdatedAt%2CdownloadCount%2CsaveCount%2CpreviewJpgLargeUrl%2CpreviewJpgSmallUrl%2Cthemes%7BhexId%2Ccreator%7Busername%7D%2Cname%2Cdescription%2CupdatedAt%2CdownloadCount%2CsaveCount%2Ctarget%2CpreviewJpgLargeUrl%2CpreviewJpgSmallUrl%2CdownloadUrl%7D%7DpageInfo%7BitemCount%2Climit%2Cpage%2CpageCount%7D%7D%7D";
        
        // Build variables JSON for packs
        variables = "{\"page\":" + std::to_string(e.page) + ",\"limit\":" + std::to_string(e.limit) + ",\"sort\":\"" + std::string{REQUEST_SORT[e.sort_index]} + "\",\"order\":\"" + std::string{REQUEST_ORDER[e.order_index]} + "\"";
        
        if (!e.query.empty()) {
            variables += ",\"query\":\"" + e.query + "\"";
        } else {
            variables += ",\"query\":null";
        }
        
        // Add includeNSFW parameter
        variables += ",\"includeNSFW\":" + std::string(e.nsfw ? "true" : "false");
        variables += "}";
    } else {
        // GraphQL query for themes with NSFW support
        query = "query%28%24target%3ATarget%2C%24page%3APositiveInt%2C%24limit%3APositiveInt%2C%24sort%3AItemSort%2C%24order%3ASortOrder%2C%24query%3AString%2C%24includeNSFW%3ABoolean%29%7BswitchThemes%28target%3A%24target%2Cpage%3A%24page%2Climit%3A%24limit%2Csort%3A%24sort%2Corder%3A%24order%2Cquery%3A%24query%2CincludeNSFW%3A%24includeNSFW%29%7Bnodes%7BhexId%2Ccreator%7Busername%7D%2Cname%2Cdescription%2CupdatedAt%2CdownloadCount%2CsaveCount%2Ctarget%2CpreviewJpgLargeUrl%2CpreviewJpgSmallUrl%2CdownloadUrl%7DpageInfo%7BitemCount%2Climit%2Cpage%2CpageCount%7D%7D%7D";
        
        // Build variables JSON for themes
        variables = "{\"page\":" + std::to_string(e.page) + ",\"limit\":" + std::to_string(e.limit) + ",\"sort\":\"" + std::string{REQUEST_SORT[e.sort_index]} + "\",\"order\":\"" + std::string{REQUEST_ORDER[e.order_index]} + "\"";
        
        if (e.target_index < 7) {
            variables += ",\"target\":\"" + std::string{REQUEST_TARGET[e.target_index]} + "\"";
        } else {
            variables += ",\"target\":null";
        }
        
        if (!e.query.empty()) {
            variables += ",\"query\":\"" + e.query + "\"";
        } else {
            variables += ",\"query\":null";
        }
        
        // Add includeNSFW parameter
        variables += ",\"includeNSFW\":" + std::string(e.nsfw ? "true" : "false");
        variables += "}";
    }
    
    // URL encode the variables
    variables = curl::EscapeString(variables);
    
    return base_url + "?query=" + query + "&variables=" + variables;
}

auto apiBuildUrlDownloadInternal(const std::string& id, bool is_pack) -> std::string {
    std::string base_url = "https://api.themezer.net/graphql";
    std::string query;
    std::string variables;
    
    if (is_pack) {
        query = "query%28%24hexId%3AString%21%29%7BswitchPack%28hexId%3A%24hexId%29%7BdownloadUrl%7D%7D";
        variables = "{\"hexId\":\"" + id + "\"}";
    } else {
        query = "query%28%24hexId%3AString%21%29%7BswitchTheme%28hexId%3A%24hexId%29%7BdownloadUrl%7D%7D";
        variables = "{\"hexId\":\"" + id + "\"}";
    }
    
    variables = curl::EscapeString(variables);
    return base_url + "?query=" + query + "&variables=" + variables;
}

auto apiBuildUrlDownloadPack(const PackListEntry& e) -> std::string {
    return apiBuildUrlDownloadInternal(e.id, true);
}

[[maybe_unused]] auto apiBuildUrlListPacks(const Config& e) -> std::string {
    return apiBuildUrlListInternal(e, true);
}

auto apiBuildListPacksCache(const Config& e) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%u_page.json", CACHE_PATH, e.page);
    return path;
}

auto apiBuildIconCache(const ThemeEntry& e) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "%s/%s_thumb.jpg", CACHE_PATH, e.id.c_str());
    return path;
}

auto loadThemeImage(ThemeEntry& e) -> bool {
    auto& image = e.preview.lazy_image;

    // already have the image
    if (e.preview.lazy_image.image) {
        // log_write("warning, tried to load image: %s when already loaded\n", path.c_str());
        return true;
    }
    auto vg = App::GetVg();

    const auto path = apiBuildIconCache(e);
    TimeStamp ts;
    const auto data = ImageLoadFromFile(path, ImageFlag_JPEG);
    if (!data.data.empty()) {
        image.w = data.w;
        image.h = data.h;
        image.image = nvgCreateImageRGBA(vg, data.w, data.h, 0, data.data.data());
        log_write("\t[image load] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    }

    if (!image.image) {
        log_write("failed to load image from file: %s\n", path.s);
        return false;
    } else {
        // log_write("loaded image from file: %s\n", path);
        return true;
    }
}

void from_json(yyjson_val* json, Creator& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
        JSON_SET_STR(display_name);
    );
    
    // 如果没有display_name但有username，使用username作为display_name
    if (e.display_name.empty()) {
        auto username_obj = yyjson_obj_get(json, "username");
        if (username_obj) {
            auto username = yyjson_get_str(username_obj);
            if (username) {
                e.display_name = username;
            }
        }
    }
}

void from_json(yyjson_val* json, Details& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(name);
    );
}

[[maybe_unused]] void from_json(yyjson_val* json, Preview& e) {
    // 解析预览图URL - 使用previewJpgSmallUrl
    auto previewSmall_obj = yyjson_obj_get(json, "previewJpgSmallUrl");
    if (previewSmall_obj) {
        auto previewSmall = yyjson_get_str(previewSmall_obj);
        if (previewSmall) {
            e.thumb = previewSmall;
            log_write("[Preview] Using previewJpgSmallUrl: %s\n", previewSmall);
        }
    }
    
    // 如果previewJpgSmallUrl为空，尝试使用thumb字段（兼容性处理）
    if (e.thumb.empty()) {
        JSON_OBJ_ITR(
            JSON_SET_STR(thumb);
        );
        
        if (!e.thumb.empty()) {
            log_write("[Preview] Using legacy thumb field: %s\n", e.thumb.c_str());
        }
    }
    
    // 如果所有URL字段都为空，记录警告
    if (e.thumb.empty()) {
        log_write("[Preview] Warning: No preview URL found (previewJpgSmallUrl and thumb are all empty or missing)\n");
    }
}

void from_json(yyjson_val* json, ThemeEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
    );
    
    // 如果没有id但有hexId，使用hexId作为id
    if (e.id.empty()) {
        auto hexId_obj = yyjson_obj_get(json, "hexId");
        if (hexId_obj) {
            auto hexId = yyjson_get_str(hexId_obj);
            if (hexId) {
                e.id = hexId;
                log_write("[ThemeEntry] Using hexId as id: %s\n", hexId);
            }
        }
    }
    
    // 解析预览图URL - 使用previewJpgSmallUrl
    auto previewSmall_obj = yyjson_obj_get(json, "previewJpgSmallUrl");
    if (previewSmall_obj) {
        auto previewSmall = yyjson_get_str(previewSmall_obj);
        if (previewSmall) {
            e.preview.thumb = previewSmall;
            log_write("[ThemeEntry] Using previewJpgSmallUrl: %s for theme: %s\n", previewSmall, e.id.c_str());
        }
    }
    
    // 如果previewJpgSmallUrl为空，尝试使用thumb字段（兼容性处理）
    if (e.preview.thumb.empty()) {
        auto thumb_obj = yyjson_obj_get(json, "thumb");
        if (thumb_obj) {
            auto thumb = yyjson_get_str(thumb_obj);
            if (thumb) {
                e.preview.thumb = thumb;
                log_write("[ThemeEntry] Using legacy thumb field: %s for theme: %s\n", thumb, e.id.c_str());
            }
        }
    }
    
    // 如果所有URL字段都为空，记录警告
    if (e.preview.thumb.empty()) {
        log_write("[ThemeEntry] Warning: No preview URL found (previewJpgSmallUrl and thumb are all empty or missing) for theme: %s\n", e.id.c_str());
    }
}

void from_json_node(yyjson_val* json, PackListEntry& e) {
    // 处理GraphQL响应中的节点格式
    JSON_OBJ_ITR(
        JSON_SET_STR(id);
    );
    
    // 如果没有id但有hexId，使用hexId作为id
    if (e.id.empty()) {
        auto hexId_obj = yyjson_obj_get(json, "hexId");
        if (hexId_obj) {
            auto hexId = yyjson_get_str(hexId_obj);
            if (hexId) {
                e.id = hexId;
                log_write("[DEBUG] from_json_node:使用hexId作为id: %s\n", hexId);
            }
        }
    }
    
    // 处理creator对象
    auto creator_obj = yyjson_obj_get(json, "creator");
    if (creator_obj) {
        from_json(creator_obj, e.creator);
    }
    
    // 处理themes数组并提取下载URL和预览图
    auto themes_obj = yyjson_obj_get(json, "themes");
    if (themes_obj && yyjson_is_arr(themes_obj)) {
        size_t idx, max;
        yyjson_val* theme_node;
        yyjson_arr_foreach(themes_obj, idx, max, theme_node) {
            ThemeEntry theme;
            
            // 解析theme的hexId
            auto theme_hexId_obj = yyjson_obj_get(theme_node, "hexId");
            if (theme_hexId_obj) {
                auto theme_hexId = yyjson_get_str(theme_hexId_obj);
                if (theme_hexId) {
                    theme.id = theme_hexId;
                }
            }
            
            // 解析预览图URL - 使用previewJpgSmallUrl
            auto previewSmall_obj = yyjson_obj_get(theme_node, "previewJpgSmallUrl");
            if (previewSmall_obj) {
                auto previewSmall = yyjson_get_str(previewSmall_obj);
                if (previewSmall) {
                    theme.preview.thumb = previewSmall;
                    log_write("[DEBUG] from_json_node:主题预览图URL: %s\n", previewSmall);
                }
            }
            
            // 解析下载URL
            auto downloadUrl_obj = yyjson_obj_get(theme_node, "downloadUrl");
            if (downloadUrl_obj) {
                auto downloadUrl = yyjson_get_str(downloadUrl_obj);
                if (downloadUrl) {
                    // 从第一个主题中提取下载URL（Pack的下载URL通常在第一个主题中）
                    if (idx == 0 && e.downloadUrl.empty()) {
                        e.downloadUrl = downloadUrl;
                        log_write("[DEBUG] from_json_node:提取到下载URL: %s\n", downloadUrl);
                    }
                }
            }
            
            e.themes.push_back(theme);
        }
    }
    
    // 如果themes数组中没有找到下载URL，尝试从pack级别获取
    if (e.downloadUrl.empty()) {
        auto downloadUrl_obj = yyjson_obj_get(json, "downloadUrl");
        if (downloadUrl_obj) {
            auto downloadUrl = yyjson_get_str(downloadUrl_obj);
            if (downloadUrl) {
                e.downloadUrl = downloadUrl;
                log_write("[DEBUG] from_json_node:从pack级别提取到下载URL: %s\n", downloadUrl);
            }
        }
    }
    
    // 如果没有details.name但有name字段，使用name字段
    if (e.details.name.empty()) {
        auto name_obj = yyjson_obj_get(json, "name");
        if (name_obj) {
            auto name = yyjson_get_str(name_obj);
            if (name) {
                e.details.name = name;
            }
        }
    }
}

[[maybe_unused]] void from_json(yyjson_val* json, PackListEntry& e) {
    JSON_OBJ_ITR(
        JSON_SET_STR(hexId);
        JSON_SET_STR(name);
        JSON_SET_STR(description);
        JSON_SET_STR(username);
        JSON_SET_STR(previewJpgSmallUrl);
        JSON_SET_STR(previewJpgLargeUrl);
        JSON_SET_OBJ(creator);
        JSON_SET_OBJ(details);
        JSON_SET_ARR_OBJ(themes);
    );
    
    // 将hexId赋值给id字段，用于下载URL构建
    if (!e.hexId.empty()) {
        e.id = e.hexId;
        log_write("[PackListEntry] Using hexId as id: %s\n", e.hexId.c_str());
    }
}

void from_json(yyjson_val* json, Pagination& e) {
    JSON_OBJ_ITR(
        JSON_SET_UINT(page);
        JSON_SET_UINT(limit);
        JSON_SET_UINT(pageCount);
        JSON_SET_UINT(itemCount);
    );
}

void from_json(const std::vector<u8>& data, DownloadPack& e) {
    // 添加调试：打印原始响应数据
    std::string response_str(data.begin(), data.end());
    log_write("[DEBUG] GraphQL Response: %s\n", response_str.c_str());
    
    JSON_INIT_VEC(data, "data");
    
    // 检查是否存在switchPack对象
    auto switchPack = yyjson_obj_get(json, "switchPack");
    if (!switchPack) {
        log_write("[ERROR] switchPack not found in response\n");
        return; // 改为直接返回，不抛出异常
    }
    
    json = switchPack;
    JSON_OBJ_ITR(
        JSON_SET_STR(downloadUrl);
    );
    
    log_write("[DEBUG] Extracted downloadUrl: %s\n", e.downloadUrl.c_str());
    
    // 检查downloadUrl是否为空
    if (e.downloadUrl.empty()) {
        log_write("[ERROR] downloadUrl is empty\n");
        return; // 改为直接返回，不抛出异常
    }
    
    // 将downloadUrl赋值给url字段，因为InstallTheme函数使用的是url字段
    e.url = e.downloadUrl;
    log_write("[DEBUG] Set url field: %s\n", e.url.c_str());
    
    // Set default values for filename and mimetype since new API doesn't provide them
    e.filename = "theme_pack.zip";
    e.mimetype = "application/zip";
}

void from_json(const fs::FsPath& path, PackList& e) {
    JSON_INIT_VEC_FILE(path, "data", nullptr);
    
    // 保存原始json指针
    yyjson_val* original_json = json;
    
    // 处理新的GraphQL响应格式
    auto switchPacks = yyjson_obj_get(json, "switchPacks");
    if (switchPacks) {
        json = switchPacks; // 使用switchPacks对象作为新的解析起点
    }
    
    // 处理pageInfo
    auto pageInfo = yyjson_obj_get(json, "pageInfo");
    if (pageInfo) {
        // 解析pageInfo到pagination
        auto page_obj = yyjson_obj_get(pageInfo, "page");
        auto limit_obj = yyjson_obj_get(pageInfo, "limit");
        auto itemCount_obj = yyjson_obj_get(pageInfo, "itemCount");
        auto pageCount_obj = yyjson_obj_get(pageInfo, "pageCount");
        
        if (page_obj) {
            auto page = yyjson_get_num(page_obj);
            if (page > 0) e.pagination.page = static_cast<u64>(page);
        }
        if (limit_obj) {
            auto limit = yyjson_get_num(limit_obj);
            if (limit > 0) e.pagination.limit = static_cast<u64>(limit);
        }
        if (itemCount_obj) {
            auto itemCount = yyjson_get_num(itemCount_obj);
            if (itemCount > 0) e.pagination.item_count = static_cast<u64>(itemCount);
        }
        if (pageCount_obj) {
            auto pageCount = yyjson_get_num(pageCount_obj);
            if (pageCount > 0) e.pagination.page_count = static_cast<u64>(pageCount);
        }
    }
    
    // 处理nodes数组包装
    auto nodes = yyjson_obj_get(json, "nodes");
    if (nodes) {
        json = nodes; // 使用nodes数组作为新的解析起点
        
        // 解析nodes数组中的packList条目
        if (yyjson_is_arr(json)) {
            size_t idx, max;
            yyjson_val* node;
            yyjson_arr_foreach(json, idx, max, node) {
                PackListEntry entry;
                from_json_node(node, entry);
                e.packs.push_back(entry);
            }
        }
    }
    
    // 恢复原始json指针以解析pagination
    json = original_json;
    
    JSON_OBJ_ITR(
        JSON_SET_OBJ(pagination);
    );
}

auto InstallTheme(ProgressBox* pbox, const PackListEntry& entry) -> Result {
    static const fs::FsPath zip_out{"/switch/sphaira/cache/themezer/temp.zip"};

    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());

    DownloadPack download_pack;

    // 1. download the zip
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Downloading "_i18n + entry.details.name);
        log_write("starting download\n");

        const auto url = apiBuildUrlDownloadPack(entry);
        log_write("using url: %s\n", url.c_str());
        const auto result = curl::Api().ToMemory(
            curl::Url{url},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        if (!result.success || result.data.empty()) {
            log_write("error with download: %s\n", url.c_str());
            R_THROW(Result_ThemezerFailedToDownloadThemeMeta);
        }

        from_json(result.data, download_pack);
    }

    // 2. download the zip
    if (!pbox->ShouldExit()) {
        pbox->NewTransfer("Downloading "_i18n + entry.details.name);
        log_write("starting download: %s\n", download_pack.url.c_str());

        const auto result = curl::Api().ToFile(
            curl::Url{download_pack.url},
            curl::Path{zip_out},
            curl::OnProgress{pbox->OnDownloadProgressCallback()}
        );

        R_UNLESS(result.success, Result_ThemezerFailedToDownloadTheme);
    }

    ON_SCOPE_EXIT(fs.DeleteFile(zip_out));

    // create directories
    fs::FsPath dir_path;
    std::snprintf(dir_path, sizeof(dir_path), "%s/%s - By %s", THEME_FOLDER.s, entry.details.name.c_str(), entry.creator.display_name.c_str());
    fs.CreateDirectoryRecursively(dir_path);

    // 3. extract the zip
    if (!pbox->ShouldExit()) {
        R_TRY(thread::TransferUnzipAll(pbox, zip_out, &fs, dir_path));
    }

    log_write("finished install :)\n");
    R_SUCCEED();
}

} // namespace

LazyImage::~LazyImage() {
    if (image) {
        nvgDeleteImage(App::GetVg(), image);
    }
}

Menu::Menu(u32 flags) : MenuBase{"Themezer"_i18n, flags} {
    fs::FsNativeSd().CreateDirectoryRecursively(CACHE_PATH);

    SetAction(Button::B, Action{"Back"_i18n, [this]{
        // if search is valid, then we are in search mode, return back to normal.
        if (!m_search.empty()) {
            m_search.clear();
            InvalidateAllPages();
        } else {
            SetPop();
        }
    }});

    this->SetActions(
        std::make_pair(Button::A, Action{"Download"_i18n, [this](){
            App::Push<OptionBox>(
                "Download theme?"_i18n,
                "Back"_i18n, "Download"_i18n, 1, [this](auto op_index){
                    if (op_index && *op_index) {
                        const auto& page = m_pages[m_page_index];
                        if (page.m_packList.size() && page.m_ready == PageLoadState::Done) {
                            const auto& entry = page.m_packList[m_index];
                            const auto url = apiBuildUrlDownloadPack(entry);

                            App::Push<ProgressBox>(
                                entry.themes[0].preview.lazy_image.image,
                                "Downloading "_i18n,
                                entry.details.name,
                                [this, &entry](auto pbox) -> Result {
                                    return InstallTheme(pbox, entry);
                                }, [this, &entry](Result rc){
                                    App::PushErrorBox(rc, "Failed to download theme"_i18n);

                                    if (R_SUCCEEDED(rc)) {
                                        App::Notify("Downloaded "_i18n + entry.details.name);
                                    }
                                }
                            );
                        }
                    }
                }
            );
        }}),
        std::make_pair(Button::X, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("Themezer Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            SidebarEntryArray::Items sort_items;
            sort_items.push_back("Downloads"_i18n);
            sort_items.push_back("Updated"_i18n);
            sort_items.push_back("Likes"_i18n);
            sort_items.push_back("ID"_i18n);

            SidebarEntryArray::Items order_items;
            order_items.push_back("Descending (down)"_i18n);
            order_items.push_back("Ascending (Up)"_i18n);

            options->Add<SidebarEntryBool>("Nsfw"_i18n, m_nsfw.Get(), [this](bool& v_out){
                m_nsfw.Set(v_out);
                InvalidateAllPages();
            });

            options->Add<SidebarEntryArray>("Sort"_i18n, sort_items, [this, sort_items](s64& index_out){
                if (m_sort.Get() != index_out) {
                    m_sort.Set(index_out);
                    InvalidateAllPages();
                }
            }, m_sort.Get());

            options->Add<SidebarEntryArray>("Order"_i18n, order_items, [this, order_items](s64& index_out){
                if (m_order.Get() != index_out) {
                    m_order.Set(index_out);
                    InvalidateAllPages();
                }
            }, m_order.Get());

            options->Add<SidebarEntryCallback>("Page"_i18n, [this](){
                s64 out;
                if (R_SUCCEEDED(swkbd::ShowNumPad(out, "Enter Page Number"_i18n.c_str(), nullptr, nullptr, -1, 3))) {
                    if (out < m_page_index_max) {
                        m_page_index = out;
                        PackListDownload();
                    } else {
                        log_write("invalid page number\n");
                        App::Notify("Bad Page"_i18n);
                    }
                }
            });

            options->Add<SidebarEntryCallback>("Search"_i18n, [this](){
                std::string out;
                if (R_SUCCEEDED(swkbd::ShowText(out)) && !out.empty()) {
                    m_search = out;
                    // PackListDownload();
                    InvalidateAllPages();
                }
            });
        }}),
        std::make_pair(Button::R2, Action{"Next"_i18n, [this](){
            m_page_index++;
            if (m_page_index >= m_page_index_max) {
                m_page_index = m_page_index_max - 1;
            } else {
                PackListDownload();
            }
        }}),
        std::make_pair(Button::L2, Action{"Prev"_i18n, [this](){
            if (m_page_index) {
                m_page_index--;
                PackListDownload();
            }
        }})
    );

    const Vec4 v{75, 110, 350, 250};
    const Vec2 pad{10, 10};
    m_list = std::make_unique<List>(3, 6, m_pos, v, pad);

    m_page_index = 0;
    m_pages.resize(1);
    PackListDownload();
}

Menu::~Menu() {

}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    if (m_pages.empty()) {
        return;
    }

    const auto& page = m_pages[m_page_index];
    if (page.m_ready != PageLoadState::Done) {
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, page.m_packList.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect::Focus);
            SetIndex(i);
        }
    });
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_pages.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty!"_i18n.c_str());
        return;
    }

    auto& page = m_pages[m_page_index];

    switch (page.m_ready) {
        case PageLoadState::None:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Not Ready..."_i18n.c_str());
            return;
        case PageLoadState::Loading:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Loading"_i18n.c_str());
            return;
        case PageLoadState::Done:
            break;
        case PageLoadState::Error:
            gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Error loading page!"_i18n.c_str());
            return;
    }

    // max images per frame, in order to not hit io / gpu too hard.
    const int image_load_max = 2;
    int image_load_count = 0;

    m_list->Draw(vg, theme, page.m_packList.size(), [this, &page, &image_load_count](auto* vg, auto* theme, auto v, auto pos) {
        const auto& [x, y, w, h] = v;
        auto& e = page.m_packList[pos];

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = pos == m_index;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            DrawElement(x, y, w, h, ThemeEntryID_GRID);
        }

        const float xoff = (350 - 320) / 2;

        // lazy load image
        if (e.themes.size()) {
            auto& theme = e.themes[0];
            auto& image = e.themes[0].preview.lazy_image;

            // try and load cached image.
            if (image_load_count < image_load_max && !image.image && !image.tried_cache) {
                image.tried_cache = true;
                image.cached = loadThemeImage(theme);
                if (image.cached) {
                    image_load_count++;
                }
            }

            if (!image.image || image.cached) {
                switch (image.state) {
                    case ImageDownloadState::None: {
                        const auto path = apiBuildIconCache(theme);
                        log_write("downloading theme!: %s\n", path.s);

                        const auto url = theme.preview.thumb;
                        log_write("downloading url: %s\n", url.c_str());
                        image.state = ImageDownloadState::Progress;
                        curl::Api().ToFileAsync(
                            curl::Url{url},
                            curl::Path{path},
                            curl::Flags{curl::Flag_Cache},
                            curl::StopToken{this->GetToken()},
                            curl::OnComplete{[this, &image](auto& result) {
                                if (result.success) {
                                    image.state = ImageDownloadState::Done;
                                    // data hasn't changed
                                    if (result.code == 304) {
                                        image.cached = false;
                                    }
                                } else {
                                    image.state = ImageDownloadState::Failed;
                                    log_write("failed to download image\n");
                                }
                            }
                        });
                    }   break;
                    case ImageDownloadState::Progress: {

                    }   break;
                    case ImageDownloadState::Done: {
                        image.cached = false;
                        if (!loadThemeImage(theme)) {
                            image.state = ImageDownloadState::Failed;
                        } else {
                            image_load_count++;
                        }
                    }   break;
                    case ImageDownloadState::Failed: {
                    }   break;
                }
            }

            gfx::drawImage(vg, x + xoff, y, 320, 180, image.image ? image.image : App::GetDefaultImage(), 5);
        }

        const auto text_x = x + xoff;
        const auto text_clip_w = w - 30.f - xoff;
        const float font_size = 18;
        m_scroll_name.Draw(vg, selected, text_x, y + 180 + 20, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.details.name.c_str());
        m_scroll_author.Draw(vg, selected, text_x, y + 180 + 55, text_clip_w, font_size, NVG_ALIGN_LEFT, theme->GetColour(text_id), e.creator.display_name.c_str());
    });
}

void Menu::OnFocusGained() {
    MenuBase::OnFocusGained();
}

void Menu::InvalidateAllPages() {
    m_pages.clear();
    m_pages.resize(1);
    m_page_index = 0;
    PackListDownload();
}

void Menu::PackListDownload() {
    const auto page_index = m_page_index + 1;
    char subheading[128];
    std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
    SetSubHeading(subheading);

    m_index = 0;
    m_list->SetYoff(0);

    // already downloaded
    if (m_pages[m_page_index].m_ready != PageLoadState::None) {
        return;
    }
    m_pages[m_page_index].m_ready = PageLoadState::Loading;

    Config config;
    config.page = page_index;
    config.SetQuery(m_search);
    config.sort_index = m_sort.Get();
    config.order_index = m_order.Get();
    config.nsfw = m_nsfw.Get();
    
    // 使用正确的函数签名构建URL
    const auto packList_url = apiBuildUrlListInternal(config, true);
    const auto packlist_path = apiBuildListPacksCache(config);

    // 添加调试信息
    log_write("\n[DEBUG] packList_url: %s\n", packList_url.c_str());
    log_write("[DEBUG] search query: %s\n", m_search.c_str());

    curl::Api().ToFileAsync(
        curl::Url{packList_url},
        curl::Header{{"User-Agent", "themezer-nx"}},
        curl::Path{packlist_path},
        curl::Flags{curl::Flag_Cache},
        curl::StopToken{this->GetToken()},
        curl::OnComplete{[this, page_index](auto& result){
            App::SetBoostMode(true);
            ON_SCOPE_EXIT(App::SetBoostMode(false));

            log_write("got themezer data\n");
            if (!result.success) {
                auto& page = m_pages[page_index-1];
                page.m_ready = PageLoadState::Error;
                log_write("failed to get themezer data...\n");
                return;
            }

            PackList a;
            from_json(result.path, a);

            m_pages.resize(a.pagination.page_count);
            auto& page = m_pages[page_index-1];

            page.m_packList = a.packs;
            page.m_pagination = a.pagination;
            page.m_ready = PageLoadState::Done;
            m_page_index_max = a.pagination.page_count;

            char subheading[128];
            std::snprintf(subheading, sizeof(subheading), "Page %zu / %zu"_i18n.c_str(), m_page_index+1, m_page_index_max);
            SetSubHeading(subheading);

            log_write("a.pagination.page: %zu\n", a.pagination.page);
            log_write("a.pagination.page_count: %zu\n", a.pagination.page_count);
        }
    });
}

} // namespace sphaira::ui::menu::themezer
