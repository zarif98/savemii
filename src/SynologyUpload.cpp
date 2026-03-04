#include <SynologyUpload.h>
#include <savemng.h>
#include <utils/LanguageUtils.h>
#include <utils/StringUtils.h>
#include <cstring>
#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>
#include <nsysnet/_socket.h>
#include <nn/ac/ac_c.h>

#define FS_ALIGN(x) ((x + 0x3F) & ~(0x3F))

const char *SynologyUpload::CONFIG_PATH = "fs:/vol/external01/wiiu/backups/synology.json";
bool SynologyUpload::networkInitialized = false;

// ─── libcurl write callback: accumulate response into std::string ───
static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t totalSize = size * nmemb;
    std::string *response = static_cast<std::string *>(userp);
    response->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

SynologyUpload::SynologyUpload()
    : enabled(false) {
}

SynologyUpload::~SynologyUpload() {
    if (!sid.empty()) {
        logout();
    }
}

// ─── Network initialization (call once) ───
bool SynologyUpload::initNetwork() {
    if (networkInitialized)
        return true;

    // Initialize Wii U networking
    ACInitialize();
    ACConfigId startupId;
    ACGetStartupId(&startupId);
    ACConnectWithConfigId(startupId);

    // Initialize socket library
    socket_lib_init();

    // Initialize libcurl globally
    curl_global_init(CURL_GLOBAL_ALL);

    networkInitialized = true;
    return true;
}

void SynologyUpload::shutdownNetwork() {
    if (!networkInitialized)
        return;

    curl_global_cleanup();
    ACFinalize();
    networkInitialized = false;
}

// ─── Config file I/O ───
bool SynologyUpload::loadConfig() {
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) {
        lastError = "Config file not found: synology.json";
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = (char *)aligned_alloc(0x40, FS_ALIGN(len + 1));
    if (!data) {
        fclose(f);
        lastError = "Memory allocation failed";
        return false;
    }

    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);

    json_error_t error;
    json_t *root = json_loads(data, 0, &error);
    free(data);

    if (!root) {
        lastError = StringUtils::stringFormat("JSON parse error: %s", error.text);
        return false;
    }

    const char *val;

    val = json_string_value(json_object_get(root, "server"));
    if (val) server = val;

    val = json_string_value(json_object_get(root, "account"));
    if (val) account = val;

    val = json_string_value(json_object_get(root, "password"));
    if (val) password = val;

    val = json_string_value(json_object_get(root, "device_id"));
    if (val) deviceId = val;

    val = json_string_value(json_object_get(root, "upload_path"));
    if (val) uploadPath = val;
    else uploadPath = "/wiiu_backups";

    json_t *enabledVal = json_object_get(root, "enabled");
    if (enabledVal)
        enabled = json_is_true(enabledVal);

    json_decref(root);
    return true;
}

bool SynologyUpload::saveConfig() {
    json_t *config = json_object();
    if (!config) return false;

    json_object_set_new(config, "server", json_string(server.c_str()));
    json_object_set_new(config, "account", json_string(account.c_str()));
    json_object_set_new(config, "password", json_string(password.c_str()));
    json_object_set_new(config, "device_id", json_string(deviceId.c_str()));
    json_object_set_new(config, "upload_path", json_string(uploadPath.c_str()));
    json_object_set_new(config, "enabled", enabled ? json_true() : json_false());

    char *configString = json_dumps(config, JSON_INDENT(2));
    json_decref(config);

    if (!configString) return false;

    FILE *fp = fopen(CONFIG_PATH, "wb");
    if (!fp) {
        free(configString);
        lastError = "Cannot write synology.json";
        return false;
    }

    fwrite(configString, strlen(configString), 1, fp);
    fclose(fp);
    free(configString);
    return true;
}

// ─── URL helpers ───
bool SynologyUpload::isQuickConnect() const {
    return server.find("quickconnect.to") != std::string::npos;
}

std::string SynologyUpload::getBaseUrl() const {
    return "https://" + server;
}

std::string SynologyUpload::getApiPath() const {
    return isQuickConnect() ? "/webapi" : "/photo/webapi";
}

// ─── HTTP GET with JSON response ───
json_t *SynologyUpload::apiGet(const std::string &url) {
    if (!networkInitialized) {
        lastError = "Network not initialized";
        return nullptr;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        lastError = "Failed to init curl";
        return nullptr;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Referer header required for QuickConnect
    if (isQuickConnect()) {
        std::string referer = "https://" + server + "/";
        curl_easy_setopt(curl, CURLOPT_REFERER, referer.c_str());
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        lastError = StringUtils::stringFormat("HTTP error: %s", curl_easy_strerror(res));
        return nullptr;
    }

    json_error_t error;
    json_t *root = json_loads(response.c_str(), 0, &error);
    if (!root) {
        lastError = StringUtils::stringFormat("JSON error: %s", error.text);
        return nullptr;
    }

    return root;
}

// ─── Authentication ───
bool SynologyUpload::login() {
    if (!networkInitialized) {
        initNetwork();
    }

    std::string baseUrl = getBaseUrl();
    std::string apiPath = getApiPath();

    // Build login URL
    std::string loginUrl = baseUrl + apiPath + "/auth.cgi"
        "?api=SYNO.API.Auth"
        "&version=6"
        "&method=login"
        "&account=" + account +
        "&passwd=" + password;

    // Add device_id for 2FA bypass
    if (!deviceId.empty()) {
        loginUrl += "&device_id=" + deviceId;
        loginUrl += "&device_name=SaveMii";
    }

    json_t *result = apiGet(loginUrl);
    if (!result) {
        return false;
    }

    json_t *success = json_object_get(result, "success");
    if (json_is_true(success)) {
        json_t *data = json_object_get(result, "data");
        const char *sidVal = json_string_value(json_object_get(data, "sid"));
        if (sidVal) {
            sid = sidVal;

            // Save device token if returned
            const char *did = json_string_value(json_object_get(data, "did"));
            if (!did) did = json_string_value(json_object_get(data, "device_id"));
            if (did && strlen(did) > 0) {
                deviceId = did;
                saveConfig();
            }

            json_decref(result);
            return true;
        }
    }

    // Login failed
    json_t *errObj = json_object_get(result, "error");
    int errCode = 0;
    if (errObj) {
        errCode = json_integer_value(json_object_get(errObj, "code"));
    }

    switch (errCode) {
        case 400: lastError = "Invalid username or password"; break;
        case 401: lastError = "Account disabled"; break;
        case 402: lastError = "Permission denied"; break;
        case 403: lastError = "Invalid 2FA code or expired device token"; break;
        case 404: lastError = "2FA required - set device_id in synology.json"; break;
        default:  lastError = StringUtils::stringFormat("Login failed (error %d)", errCode); break;
    }

    json_decref(result);
    return false;
}

void SynologyUpload::logout() {
    if (sid.empty()) return;

    std::string url = getBaseUrl() + getApiPath() + "/auth.cgi"
        "?api=SYNO.API.Auth&version=6&method=logout&_sid=" + sid;

    json_t *result = apiGet(url);
    if (result) json_decref(result);
    sid.clear();
}

// ─── Create remote folder ───
bool SynologyUpload::createRemoteFolder(const std::string &folderPath, const std::string &name) {
    std::string url = getBaseUrl() + getApiPath() + "/entry.cgi"
        "?api=SYNO.FileStation.CreateFolder"
        "&version=2"
        "&method=create"
        "&folder_path=" + folderPath +
        "&name=" + name +
        "&force_parent=true"
        "&_sid=" + sid;

    json_t *result = apiGet(url);
    if (!result) return false;

    bool ok = json_is_true(json_object_get(result, "success"));
    if (!ok) {
        json_t *errObj = json_object_get(result, "error");
        int code = errObj ? json_integer_value(json_object_get(errObj, "code")) : 0;
        // Error 1100 = folder already exists, that's OK
        if (code == 1100) ok = true;
        else lastError = StringUtils::stringFormat("CreateFolder failed (error %d)", code);
    }

    json_decref(result);
    return ok;
}

// ─── Upload a single file via multipart POST ───
bool SynologyUpload::apiUploadFile(const std::string &url, const std::string &localFilePath,
                                    const std::string &destPath, const std::string &filename) {
    // Read the local file
    uint8_t *fileData = nullptr;
    int32_t fileSize = readLocalFile(localFilePath, &fileData);
    if (fileSize < 0 || !fileData) {
        lastError = "Cannot read local file: " + localFilePath;
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(fileData);
        lastError = "Failed to init curl";
        return false;
    }

    // Build multipart form
    curl_mime *mime = curl_mime_init(curl);

    // API parameters
    curl_mimepart *part;

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "api");
    curl_mime_data(part, "SYNO.FileStation.Upload", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "version");
    curl_mime_data(part, "2", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "method");
    curl_mime_data(part, "upload", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "path");
    curl_mime_data(part, destPath.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "create_parents");
    curl_mime_data(part, "true", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "overwrite");
    curl_mime_data(part, "true", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "_sid");
    curl_mime_data(part, sid.c_str(), CURL_ZERO_TERMINATED);

    // The actual file
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_data(part, (const char *)fileData, fileSize);
    curl_mime_filename(part, filename.c_str());
    curl_mime_type(part, "application/octet-stream");

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (isQuickConnect()) {
        std::string referer = "https://" + server + "/";
        curl_easy_setopt(curl, CURLOPT_REFERER, referer.c_str());
    }

    CURLcode res = curl_easy_perform(curl);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    free(fileData);

    if (res != CURLE_OK) {
        lastError = StringUtils::stringFormat("Upload error: %s", curl_easy_strerror(res));
        return false;
    }

    // Parse response
    json_error_t error;
    json_t *root = json_loads(response.c_str(), 0, &error);
    if (!root) {
        lastError = "Upload response parse error";
        return false;
    }

    bool ok = json_is_true(json_object_get(root, "success"));
    if (!ok) {
        json_t *errObj = json_object_get(root, "error");
        int code = errObj ? json_integer_value(json_object_get(errObj, "code")) : 0;
        lastError = StringUtils::stringFormat("Upload failed (error %d)", code);
    }

    json_decref(root);
    return ok;
}

bool SynologyUpload::uploadFile(const std::string &localPath, const std::string &remotePath) {
    if (sid.empty()) {
        lastError = "Not logged in";
        return false;
    }

    // Extract filename from path
    std::string filename = localPath;
    size_t lastSlash = filename.rfind('/');
    if (lastSlash != std::string::npos)
        filename = filename.substr(lastSlash + 1);

    std::string uploadUrl = getBaseUrl() + getApiPath() + "/entry.cgi";

    return apiUploadFile(uploadUrl, localPath, remotePath, filename);
}

// ─── Read a local file into a buffer ───
int32_t SynologyUpload::readLocalFile(const std::string &path, uint8_t **buf) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    int32_t size = (int32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    *buf = (uint8_t *)aligned_alloc(0x40, FS_ALIGN(size));
    if (!*buf) {
        fclose(f);
        return -1;
    }

    fread(*buf, 1, size, f);
    fclose(f);
    return size;
}

// ─── Upload entire directory recursively ───
bool SynologyUpload::uploadDirectory(const std::string &srcDir, const std::string &dstDir) {
    if (sid.empty()) {
        lastError = "Not logged in";
        return false;
    }

    // Create the destination folder on NAS
    // Split dstDir into parent and name
    std::string parent = dstDir;
    std::string name;
    size_t lastSlash = parent.rfind('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        name = parent.substr(lastSlash + 1);
        parent = parent.substr(0, lastSlash);
    } else {
        name = parent;
        parent = "/";
    }

    createRemoteFolder(parent, name);

    DIR *dir = opendir(srcDir.c_str());
    if (!dir) {
        lastError = "Cannot open local directory: " + srcDir;
        return false;
    }

    bool allOk = true;
    struct dirent *entry;

    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        std::string localPath = srcDir + "/" + entry->d_name;
        std::string remoteName = std::string(entry->d_name);

        struct stat st;
        if (stat(localPath.c_str(), &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            if (!uploadDirectory(localPath, dstDir + "/" + remoteName))
                allOk = false;
        } else {
            // Upload file
            if (!uploadFile(localPath, dstDir))
                allOk = false;
        }
    }

    closedir(dir);
    return allOk;
}
