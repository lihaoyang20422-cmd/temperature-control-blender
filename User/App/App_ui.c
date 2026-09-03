#include "App_ui.h"
#include "App_system.h"
#include "App_oled.h"

#define APP_UI_UPDATE_PERIOD_MS 100U
#define APP_UI_SPEED_FILTER_SCALE 256L
#define APP_UI_SPEED_FILTER_DIVISOR 4L

static TaskHandle_t s_uiTaskHandle = NULL;
static volatile AppUiPage_t s_uiPage = APP_UI_PAGE_HOME;

static int16_t App_UiFilterSpeedForDisplay(int16_t currentSpeed,
                                           AppMotorStatusValue_t status)
{
    static int32_t filteredSpeedScaled;
    static uint8_t filterInitialized;
    int32_t currentSpeedScaled;

    /*
     * 显示滤波仅作用于 UI 任务持有的状态快照，不回写 g_appData，
     * 因而不会给电机 PID 引入额外延迟。IDLE 和 FAULT 必须立即显示 0，
     * 同时清除滤波历史，保证下一次启动不会沿用上一次运行的转速。
     */
    /* FAULT 必须立即显示零；IDLE 允许显示电机停止后的自然减速过程。 */
    if ((status == APP_MOTOR_STATUS_FAULT) || (currentSpeed <= 0))
    {
        filteredSpeedScaled = 0;
        filterInitialized = 0U;
        return 0;
    }

    currentSpeedScaled = (int32_t)currentSpeed * APP_UI_SPEED_FILTER_SCALE;
    if (filterInitialized == 0U)
    {
        /* 首个有效转速直接作为初值，避免 OLED 从 0 缓慢爬升。 */
        filteredSpeedScaled = currentSpeedScaled;
        filterInitialized = 1U;
    }
    else
    {
        /* 一阶低通：新显示值 = 旧值 + (当前值 - 旧值) / 4。 */
        filteredSpeedScaled +=
            (currentSpeedScaled - filteredSpeedScaled) /
            APP_UI_SPEED_FILTER_DIVISOR;
    }

    return (int16_t)((filteredSpeedScaled +
                      (APP_UI_SPEED_FILTER_SCALE / 2L)) /
                     APP_UI_SPEED_FILTER_SCALE);
}

static uint8_t App_UiHomeChanged(const AppData_t *currentData,
                                 const AppData_t *displayedData)
{
    return ((currentData->RtcTime.Year != displayedData->RtcTime.Year) ||
            (currentData->RtcTime.Month != displayedData->RtcTime.Month) ||
            (currentData->RtcTime.Date != displayedData->RtcTime.Date) ||
            (currentData->RtcTime.Hours != displayedData->RtcTime.Hours) ||
            (currentData->RtcTime.Minutes != displayedData->RtcTime.Minutes) ||
            (currentData->RtcTime.Seconds != displayedData->RtcTime.Seconds)) ? 1U : 0U;
}

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

AppUiPage_t App_UiGetPage(void)
{
    return s_uiPage;
}

uint8_t App_UiEnterSettingsPage(void)
{
    if (s_uiPage != APP_UI_PAGE_HOME)
    {
        return 0U;
    }

    s_uiPage = APP_UI_PAGE_SETTINGS;
    App_UiNotify();
    return 1U;
}

void App_UiTask(void *argument)
{
    AppData_t dataSnapshot;
    AppFocusState_t focusSnapshot;
    AppData_t displayedData = { 0 };
    AppFocusState_t displayedFocus = { APP_FOCUS_TEMPERATURE, APP_FOCUS_TEMPERATURE };
    AppUiPage_t currentPage;
    AppUiPage_t displayedPage = APP_UI_PAGE_HOME;
    uint8_t firstFrame = 1U;
    uint8_t updateOk;

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

            /* 解锁后只平滑 OLED 使用的快照，PID 继续使用全局状态中的真实转速。 */
            dataSnapshot.CurrentSpeed =
                App_UiFilterSpeedForDisplay(dataSnapshot.CurrentSpeed,
                                            dataSnapshot.CurrentStatus);
            currentPage = s_uiPage;

            /* 100 ms 周期只负责检查状态，显示内容不变时不占用 I2C1 总线。 */
            if ((firstFrame != 0U) || (currentPage != displayedPage) ||
                ((currentPage == APP_UI_PAGE_HOME) &&
                 (App_UiHomeChanged(&dataSnapshot, &displayedData) != 0U)) ||
                ((currentPage == APP_UI_PAGE_SETTINGS) &&
                 (App_UiDisplayChanged(&dataSnapshot, &focusSnapshot,
                                       &displayedData, &displayedFocus) != 0U)))
            {
                updateOk = (currentPage == APP_UI_PAGE_HOME) ?
                           App_OledUpdateHome(&dataSnapshot) :
                           App_OledUpdate(&dataSnapshot, &focusSnapshot);
                if (updateOk != 0U)
                {
                    displayedData = dataSnapshot;
                    displayedFocus = focusSnapshot;
                    displayedPage = currentPage;
                    firstFrame = 0U;
                }
            }
        }
    }
}
