#pragma once

#include <cstdint>
#include <jansson.h>
#include <string>

/**
 * Synology NAS uploader via File Station API over HTTPS.
 * Uses libcurl + mbedTLS for QuickConnect remote access.
 *
 * Config is read from SD card: fs:/vol/external01/wiiu/backups/synology.json
 * {
 *   "server": "mynas.quickconnect.to",
 *   "account": "username",
 *   "password": "password",
 *   "device_id": "",
 *   "upload_path": "/wiiu_backups",
 *   "enabled": true
 * }
 */

class SynologyUpload {
public:
    SynologyUpload();
    ~SynologyUpload();

    // Initialize networking (call once at app startup)
    static bool initNetwork();
    static void shutdownNetwork();

    // Load config from SD card
    bool loadConfig();

    // Save config (e.g. after getting device_id from 2FA)
    bool saveConfig();

    // Check if upload is configured and enabled
    bool isEnabled() const { return enabled && !server.empty() && !account.empty(); }

    // Authenticate with Synology API
    // Returns true if login successful. Sets sid internally.
    bool login();

    // Login with OTP code for initial 2FA setup. Requests a device token.
    // On success, device_id is saved to config automatically.
    bool loginWithOtp(const std::string &otpCode);

    // Get the last login error code (Synology API error code)
    int getLastLoginErrorCode() const { return lastLoginErrorCode; }

    // Check if a device token is registered
    bool hasDeviceToken() const { return !deviceId.empty(); }

    // Clear stored device token
    void clearDeviceToken() { deviceId = ""; }

    // Logout / release session
    void logout();

    // Upload a single file to the NAS
    // localPath: full path on Wii U filesystem (e.g. "fs:/vol/external01/wiiu/backups/...")
    // remotePath: destination folder on NAS (e.g. "/wiiu_backups/00050000101C9300/0")
    bool uploadFile(const std::string &localPath, const std::string &remotePath);

    // Upload an entire directory recursively
    // srcDir: local directory path
    // dstDir: remote destination base path
    bool uploadDirectory(const std::string &srcDir, const std::string &dstDir);

    // Create a remote directory on the NAS
    bool createRemoteFolder(const std::string &folderPath, const std::string &name);

    // Get last error message
    const std::string &getLastError() const { return lastError; }

    // Getters for UI display
    const std::string &getServer() const { return server; }
    const std::string &getAccount() const { return account; }
    const std::string &getUploadPath() const { return uploadPath; }
    int getAutoBackupMinutes() const { return autoBackupMinutes; }

    // Setters for config UI
    void setServer(const std::string &s) { server = s; }
    void setAccount(const std::string &a) { account = a; }
    void setPassword(const std::string &p) { password = p; }
    void setUploadPath(const std::string &p) { uploadPath = p; }
    void setEnabled(bool e) { enabled = e; }

private:
    // Build the base URL for API requests
    std::string getBaseUrl() const;

    // Get the API path prefix (/webapi for QuickConnect, /photo/webapi otherwise)
    std::string getApiPath() const;

    // Check if server is a QuickConnect address
    bool isQuickConnect() const;

    // Perform an HTTP GET and return parsed JSON (caller must json_decref)
    json_t *apiGet(const std::string &url);

    // Perform an HTTP POST with multipart form data for file upload
    bool apiUploadFile(const std::string &url, const std::string &localFilePath,
                       const std::string &destPath, const std::string &filename);

    // Read a local file into a buffer (returns size, -1 on error)
    int32_t readLocalFile(const std::string &path, uint8_t **buf);

    static const char *CONFIG_PATH;

    std::string server;
    std::string account;
    std::string password;
    std::string deviceId;
    std::string uploadPath;
    bool enabled;
    int autoBackupMinutes; // 0 = disabled, >0 = interval in minutes

    std::string sid; // Session ID from login
    std::string lastError;
    int lastLoginErrorCode = 0;

    static bool networkInitialized;
};
