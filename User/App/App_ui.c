#include "App_ui.h"
#include "App_system.h"
#include "App_oled.h"

#define APP_UI_UPDATE_PERIOD_MS 100U

static TaskHandle_t s_uiTaskHandle = NULL;

/* 只比较 OLED 实际显示的字段，避免数据未变化时重复传输整帧。 */
static uint8_t App_UiDisplayChanged(const AppData_t *currentData,
                                    const AppFocusState_t *currentFocus,
                                    const AppData_t *displayedData,
                                    const AppFocusState_t *displayedFocus)
{
    uint8_t index;

    if ((currentData->CurrentTemperature != displayedData->CurrentTemperature) ||
        (currentData->CurrentBoardTemperature != displayedData->CurrentBoardTemperature) ||
        (currentData->TargetTemperature != displayedData->TargetTemperature) ||
        (currentData->CurrentSpeed != displayedData->CurrentSpeed) ||
        (currentData->TargetSpeed != displayedData->TargetSpeed) ||
        (currentData->CurrentTime != displayedData->CurrentTime) ||
        (currentData->RemainingTime != displayedData->RemainingTime) ||
        (currentData->TargetTime != displayedData->TargetTime) ||
        (currentData->CurrentStatus != displayedData->CurrentStatus) ||
        (currentFocus->Current != displayedFocus->Current))
    {
        return 1U;
    }

    for (index = 0U; index < APP_UID_WORD_COUNT; index++)
    {
        if (currentData->Uid[index] != displayedData->Uid[index])
        {
            return 1U;
        }
    }

    return 0U;
}

void App_UiNotify(void)
{
    /* UI 任务尚未运行时不发送通知，任务启动后会按 100 ms 周期自动刷新。 */
    if (s_uiTaskHandle != NULL)
    {
        (void)xTaskNotifyGive(s_uiTaskHandle);
    }
}

void App_UiTask(void *argument)
{
    AppData_t dataSnapshot;
    AppFocusState_t focusSnapshot;
    AppData_t displayedData = { 0 };
    AppFocusState_t displayedFocus = { APP_FOCUS_TEMPERATURE, APP_FOCUS_TEMPERATURE };
    uint8_t firstFrame = 1U;

    (void)argument;
    s_uiTaskHandle = xTaskGetCurrentTaskHandle();

    for (;;)
    {
        /* 通知立即唤醒；没有通知时最多等待 100 ms，保证界面不会长时间不更新。 */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_UI_UPDATE_PERIOD_MS));

        /* 只在锁内复制共享状态，禁止持锁执行 OLED I2C 事务。 */
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            dataSnapshot = g_appData;
            focusSnapshot = g_focusState;
            App_SystemUnlock();

            /* 100 ms 周期只负责检查状态，显示内容不变时不占用 I2C1 总线。 */
            if ((firstFrame != 0U) ||
                (App_UiDisplayChanged(&dataSnapshot, &focusSnapshot,
                                      &displayedData, &displayedFocus) != 0U))
            {
                if (App_OledUpdate(&dataSnapshot, &focusSnapshot) != 0U)
                {
                    displayedData = dataSnapshot;
                    displayedFocus = focusSnapshot;
                    firstFrame = 0U;
                }
            }
        }
    }
}
