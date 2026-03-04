#include <menu/SynologySettingsState.h>
#include <menu/KeyboardState.h>
#include <savemng.h>
#include <SynologyUpload.h>
#include <utils/InputUtils.h>
#include <utils/LanguageUtils.h>
#include <utils/DrawUtils.h>
#include <utils/Colors.h>

static int cursorPos = 0;

// Access the global uploader (defined in savemng.cpp)
extern SynologyUpload *getSynologyUploader();

SynologySettingsState::SynologySettingsState() {
    // Load current values from the uploader config
    SynologyUpload *uploader = getSynologyUploader();
    if (uploader) {
        enabled = uploader->isEnabled();
        server = uploader->getServer();
        account = uploader->getAccount();
        uploadPath = uploader->getUploadPath();
        autoBackupMinutes = uploader->getAutoBackupMinutes();
        password = ""; // Don't display existing password
    } else {
        enabled = false;
        server = "";
        account = "";
        password = "";
        uploadPath = "/wiiu_backups";
        autoBackupMinutes = 0;
    }

    // Find matching auto-backup preset index
    currentAutoPresetIndex = 0;
    for (int i = 0; i < autoBackupPresetCount; i++) {
        if (autoBackupPresets[i] == autoBackupMinutes) {
            currentAutoPresetIndex = i;
            break;
        }
    }

    cursorPos = 0;
    statusMessage = "";
}

void SynologySettingsState::render() {
    if (this->state == STATE_DO_SUBSTATE) {
        if (this->subState == nullptr) {
            OSFatal("SubState was null");
        }
        this->subState->render();
        return;
    }
    if (this->state == STATE_SYNOLOGY_MENU) {
        DrawUtils::setFontColor(COLOR_INFO);
        consolePrintPosAligned(0, 4, 1, LanguageUtils::gettext("Synology NAS Settings"));

        // Enabled
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_ENABLED);
        consolePrintPos(M_OFF, 2, LanguageUtils::gettext("   Upload Enabled: %s"),
            enabled ? LanguageUtils::gettext("Yes") : LanguageUtils::gettext("No"));

        // Server
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_SERVER);
        consolePrintPos(M_OFF, 3, LanguageUtils::gettext("   Server: %s"),
            server.empty() ? "(not set)" : server.c_str());

        // Account
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_ACCOUNT);
        consolePrintPos(M_OFF, 4, LanguageUtils::gettext("   Account: %s"),
            account.empty() ? "(not set)" : account.c_str());

        // Password
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_PASSWORD);
        if (password.empty()) {
            consolePrintPos(M_OFF, 5, LanguageUtils::gettext("   Password: (unchanged)"));
        } else {
            std::string masked(password.length(), '*');
            consolePrintPos(M_OFF, 5, LanguageUtils::gettext("   Password: %s"), masked.c_str());
        }

        // Upload path
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_UPLOAD_PATH);
        consolePrintPos(M_OFF, 6, LanguageUtils::gettext("   Upload Path: %s"),
            uploadPath.empty() ? "/wiiu_backups" : uploadPath.c_str());

        // Auto-backup
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_AUTO_BACKUP);
        if (autoBackupMinutes <= 0) {
            consolePrintPos(M_OFF, 7, LanguageUtils::gettext("   Auto-Backup: Disabled"));
        } else if (autoBackupMinutes < 60) {
            consolePrintPos(M_OFF, 7, LanguageUtils::gettext("   Auto-Backup: Every %d min"), autoBackupMinutes);
        } else {
            consolePrintPos(M_OFF, 7, LanguageUtils::gettext("   Auto-Backup: Every %d hr"), autoBackupMinutes / 60);
        }

        // Test connection
        DrawUtils::setFontColorByCursor(COLOR_TEXT, COLOR_TEXT_AT_CURSOR, cursorPos, FIELD_TEST_CONNECTION);
        consolePrintPos(M_OFF, 9, LanguageUtils::gettext("   [ Test Connection ]"));

        // Status message
        if (!statusMessage.empty()) {
            DrawUtils::setFontColor(statusIsError ? COLOR_LIST_DANGER : COLOR_BG_SYNOLOGY);
            consolePrintPos(M_OFF + 2, 11, "%s", statusMessage.c_str());
        }

        // Cursor
        DrawUtils::setFontColor(COLOR_TEXT);
        int yPos = (cursorPos < FIELD_TEST_CONNECTION) ? (2 + cursorPos) : 9;
        consolePrintPos(M_OFF, yPos, "\u2192");

        consolePrintPosAligned(17, 4, 2,
            LanguageUtils::gettext("\ue045: Save  \ue000: Edit  \ue001: Back"));
    }
}

ApplicationState::eSubState SynologySettingsState::update(Input *input) {
    if (this->state == STATE_SYNOLOGY_MENU) {
        if (input->get(TRIGGER, PAD_BUTTON_B))
            return SUBSTATE_RETURN;

        if (input->get(TRIGGER, PAD_BUTTON_UP)) {
            if (--cursorPos < 0)
                cursorPos = 0;
        }
        if (input->get(TRIGGER, PAD_BUTTON_DOWN)) {
            if (++cursorPos >= FIELD_COUNT)
                cursorPos = FIELD_COUNT - 1;
        }

        // Left/Right for toggle fields
        if (input->get(TRIGGER, PAD_BUTTON_LEFT) || input->get(TRIGGER, PAD_BUTTON_RIGHT)) {
            int dir = input->get(TRIGGER, PAD_BUTTON_RIGHT) ? 1 : -1;
            switch (cursorPos) {
                case FIELD_ENABLED:
                    enabled = !enabled;
                    break;
                case FIELD_AUTO_BACKUP:
                    currentAutoPresetIndex += dir;
                    if (currentAutoPresetIndex < 0) currentAutoPresetIndex = 0;
                    if (currentAutoPresetIndex >= autoBackupPresetCount) currentAutoPresetIndex = autoBackupPresetCount - 1;
                    autoBackupMinutes = autoBackupPresets[currentAutoPresetIndex];
                    break;
                default:
                    break;
            }
        }

        // A button: edit text fields or test connection
        if (input->get(TRIGGER, PAD_BUTTON_A)) {
            switch (cursorPos) {
                case FIELD_SERVER:
                    keyboardBuffer = server;
                    editingField = FIELD_SERVER;
                    this->state = STATE_DO_SUBSTATE;
                    this->subState = std::make_unique<KeyboardState>(keyboardBuffer);
                    break;
                case FIELD_ACCOUNT:
                    keyboardBuffer = account;
                    editingField = FIELD_ACCOUNT;
                    this->state = STATE_DO_SUBSTATE;
                    this->subState = std::make_unique<KeyboardState>(keyboardBuffer);
                    break;
                case FIELD_PASSWORD:
                    keyboardBuffer = "";
                    editingField = FIELD_PASSWORD;
                    this->state = STATE_DO_SUBSTATE;
                    this->subState = std::make_unique<KeyboardState>(keyboardBuffer);
                    break;
                case FIELD_UPLOAD_PATH:
                    keyboardBuffer = uploadPath;
                    editingField = FIELD_UPLOAD_PATH;
                    this->state = STATE_DO_SUBSTATE;
                    this->subState = std::make_unique<KeyboardState>(keyboardBuffer);
                    break;
                case FIELD_TEST_CONNECTION: {
                    statusMessage = "Testing connection...";
                    statusIsError = false;
                    DrawUtils::setRedraw(true);

                    SynologyUpload *uploader = getSynologyUploader();
                    if (!uploader) {
                        statusMessage = "Error: Uploader not initialized";
                        statusIsError = true;
                        break;
                    }

                    // Temporarily apply settings for the test
                    uploader->setServer(server);
                    uploader->setAccount(account);
                    if (!password.empty())
                        uploader->setPassword(password);
                    uploader->setUploadPath(uploadPath);

                    if (!SynologyUpload::initNetwork()) {
                        statusMessage = "Network init failed";
                        statusIsError = true;
                        break;
                    }

                    if (uploader->login()) {
                        statusMessage = "Connected! Login successful.";
                        statusIsError = false;
                        uploader->logout();
                    } else {
                        statusMessage = uploader->getLastError();
                        statusIsError = true;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // + button: save config
        if (input->get(TRIGGER, PAD_BUTTON_PLUS)) {
            SynologyUpload *uploader = getSynologyUploader();
            if (uploader) {
                uploader->setServer(server);
                uploader->setAccount(account);
                if (!password.empty())
                    uploader->setPassword(password);
                uploader->setUploadPath(uploadPath);
                uploader->setEnabled(enabled);

                if (uploader->saveConfig()) {
                    statusMessage = "Configuration saved!";
                    statusIsError = false;
                } else {
                    statusMessage = "Failed to save config";
                    statusIsError = true;
                }
            } else {
                statusMessage = "Error: Uploader not initialized";
                statusIsError = true;
            }
        }

    } else if (this->state == STATE_DO_SUBSTATE) {
        auto retSubState = this->subState->update(input);
        if (retSubState == SUBSTATE_RUNNING) {
            return SUBSTATE_RUNNING;
        } else if (retSubState == SUBSTATE_RETURN) {
            // Apply keyboard result to the correct field
            switch (editingField) {
                case FIELD_SERVER:
                    server = keyboardBuffer;
                    break;
                case FIELD_ACCOUNT:
                    account = keyboardBuffer;
                    break;
                case FIELD_PASSWORD:
                    password = keyboardBuffer;
                    break;
                case FIELD_UPLOAD_PATH:
                    uploadPath = keyboardBuffer;
                    break;
                default:
                    break;
            }
            editingField = FIELD_COUNT;
            this->subState.reset();
            this->state = STATE_SYNOLOGY_MENU;
        }
    }
    return SUBSTATE_RUNNING;
}
