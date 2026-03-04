#pragma once

#include <ApplicationState.h>
#include <memory>
#include <utils/InputUtils.h>

class SynologySettingsState : public ApplicationState {
public:
    SynologySettingsState();

    enum eState {
        STATE_SYNOLOGY_MENU,
        STATE_DO_SUBSTATE,
    };

    enum eField {
        FIELD_ENABLED = 0,
        FIELD_SERVER,
        FIELD_ACCOUNT,
        FIELD_PASSWORD,
        FIELD_UPLOAD_PATH,
        FIELD_AUTO_BACKUP,
        FIELD_2FA_STATUS,
        FIELD_TEST_CONNECTION,
        FIELD_COUNT
    };

    void render() override;
    ApplicationState::eSubState update(Input *input) override;

private:
    std::unique_ptr<ApplicationState> subState{};
    eState state = STATE_SYNOLOGY_MENU;

    // Editable fields (loaded from config on construction)
    bool enabled;
    std::string server;
    std::string account;
    std::string password;
    std::string uploadPath;
    int autoBackupMinutes;

    // Which field is being edited via keyboard
    eField editingField = FIELD_COUNT;

    // Keyboard buffer for text input
    std::string keyboardBuffer;

    // Auto-backup presets (minutes)
    static constexpr int autoBackupPresets[] = {0, 15, 30, 60, 120, 240};
    static constexpr int autoBackupPresetCount = 6;
    int currentAutoPresetIndex = 0;

    // Status message from test connection
    std::string statusMessage;
    bool statusIsError = false;
};
