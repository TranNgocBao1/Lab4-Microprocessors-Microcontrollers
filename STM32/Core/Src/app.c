/* ======================================================================
 * app.c - Lab 4 Cooperative Scheduler (STM32F103C6, Proteus)
 *
 * Sơ đồ nối (LED active-high: chân = 1 thì sáng):
 *   PA3 = D1 (0.5 s)   PA4 = D2 (1.0 s)   PA5 = D3 (1.5 s)
 *   PA6 = D4 (2.0 s)   PA7 = D5 (2.5 s)   PA8 = D6 (one-shot)
 *   PA9 = USART1_TX -> RXD của Virtual Terminal (115200, 8N1)
 *
 * Nội dung:
 *   1. 5 task CHU KỲ  (đề mục 4)           : D1..D5
 *   2. Task ONE-SHOT                        : D6 sáng 1 s rồi tự tắt, lặp mỗi 5 s
 *                                             (task chu kỳ sinh ra one-shot lúc chạy)
 *   3. Demo theo đề mục 5                   : callback 10 ms in get_time(),
 *                                             nhiều timeout đồng thời, one-shot thêm
 *                                             trước vòng lặp chính, xóa task
 * ====================================================================== */
#include "app.h"
#include "scheduler.h"
#include "main.h"
#include <stdio.h>

extern TIM_HandleTypeDef  htim2;
extern UART_HandleTypeDef huart1;

/* ---------------- Cấu hình theo schematic ---------------- */
#define LED_PORT   GPIOA
#define PIN_D1     GPIO_PIN_3
#define PIN_D2     GPIO_PIN_4
#define PIN_D3     GPIO_PIN_5
#define PIN_D4     GPIO_PIN_6
#define PIN_D5     GPIO_PIN_7
#define PIN_D6     GPIO_PIN_8        /* LED one-shot */

/* ---------------- Thông số demo (ms) ---------------- */
#define MS2TICK(ms)            ((ms) / TICK_MS)
#define D6_TRIGGER_FIRST_MS    1200  /* lần kích đầu tiên của D6 */
#define D6_TRIGGER_PERIOD_MS   3000  /* D6 được kích lại mỗi 5 s */
#define D6_PULSE_MS            1000  /* D6 sáng bao lâu thì one-shot tắt */
#define DEMO_TICK10_RUN_MS     2000  /* callback 10 ms chạy bao lâu rồi bị xóa (0 = chạy mãi) */

static uint32_t tick10_id = NO_TASK_ID;

/* In "[ thời_gian ms] nội dung" ra USART1 (Virtual Terminal) */
static void log_msg(const char *text)
{
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "[%6lu ms] %s\r\n",
                     (unsigned long)get_time(), text);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;   /* snprintf bị cắt: không gửi quá buffer */
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 20);
}

/* ======================================================================
 * 1. PERIODIC TASKS: 0.5 s, 1 s, 1.5 s, 2 s, 2.5 s
 * ====================================================================== */
static void Task_D1(void) { HAL_GPIO_TogglePin(LED_PORT, PIN_D1); log_msg("0.5s"); }
static void Task_D2(void) { HAL_GPIO_TogglePin(LED_PORT, PIN_D2); log_msg("1.0s"); }
static void Task_D3(void) { HAL_GPIO_TogglePin(LED_PORT, PIN_D3); log_msg("1.5s"); }
static void Task_D4(void) { HAL_GPIO_TogglePin(LED_PORT, PIN_D4); log_msg("2.0s"); }
static void Task_D5(void) { HAL_GPIO_TogglePin(LED_PORT, PIN_D5); log_msg("2.5s"); }

/* ======================================================================
 * 2. ONE-SHOT TASK trên D6 (PA8)
 *    Task_D6_Trigger là task CHU KỲ (mỗi 5 s). Mỗi lần chạy nó bật D6 rồi
 *    đăng ký một task ONE-SHOT (Period = 0) để tắt D6 sau 1 s.
 *    OneShot_D6_Off chạy đúng 1 lần rồi tự được scheduler giải phóng.
 * ====================================================================== */
static void OneShot_D6_Off(void)
{
    HAL_GPIO_WritePin(LED_PORT, PIN_D6, GPIO_PIN_RESET);
    log_msg("D6 OFF (one-shot)");
}

static void Task_D6_Trigger(void)
{
    HAL_GPIO_WritePin(LED_PORT, PIN_D6, GPIO_PIN_SET);
    log_msg("D6 ON  (+1s one-shot)");
    if (SCH_Add_Task(OneShot_D6_Off, MS2TICK(D6_PULSE_MS), 0) == NO_TASK_ID) {
        /* hết chỗ trong pool: tắt ngay, tránh D6 sáng mãi */
        HAL_GPIO_WritePin(LED_PORT, PIN_D6, GPIO_PIN_RESET);
    }
}

/* ======================================================================
 * 3. DEMO theo đề mục 5
 * ====================================================================== */
/* Callback mỗi 10 ms, in giá trị get_time() mỗi lần được gọi */
static void Task_Tick10(void) { log_msg("tick10"); }

/* Dùng SCH_Delete_Task: dừng callback 10 ms sau DEMO_TICK10_RUN_MS */
static void Task_StopTick10(void)
{
    if (SCH_Delete_Task(tick10_id) == RETURN_NORMAL) log_msg("tick10 DELETED");
    else                                             log_msg("delete FAILED");
}

/* Các one-shot đăng ký TRƯỚC vòng lặp chính, chỉ in timestamp khi hết delay */
static void OneShot_A(void) { log_msg("one-shot A (1.5 s)"); }
static void OneShot_B(void) { log_msg("one-shot B (3.0 s)"); }
static void OneShot_C(void) { log_msg("one-shot C (5.0 s)"); }

/* ======================================================================
 * Ngắt timer: chỉ gọi SCH_Update (rất ngắn, O(1))
 * ====================================================================== */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        SCH_Update();
    }
}

/* ======================================================================
 * Khởi tạo: đăng ký toàn bộ task rồi mới bật timer
 * ====================================================================== */
void App_Init(void)
{
    SCH_Init();

    /* --- 5 task chu kỳ. Delay (tick) so le 1..5 để không trùng thời điểm đến hạn --- */
    SCH_Add_Task(Task_D1, 1, MS2TICK(500));
    SCH_Add_Task(Task_D2, 2, MS2TICK(1000));
    SCH_Add_Task(Task_D3, 3, MS2TICK(1500));
    SCH_Add_Task(Task_D4, 4, MS2TICK(2000));
    SCH_Add_Task(Task_D5, 5, MS2TICK(2500));

    /* --- Task chu kỳ kích hoạt one-shot trên D6 --- */
    SCH_Add_Task(Task_D6_Trigger, MS2TICK(D6_TRIGGER_FIRST_MS), MS2TICK(D6_TRIGGER_PERIOD_MS));

    /* --- Demo: callback 10 ms (Delay 0 = chạy ngay), chạy song song các task trên --- */
    tick10_id = SCH_Add_Task(Task_Tick10, 0, MS2TICK(10));
    if (DEMO_TICK10_RUN_MS > 0) {
        SCH_Add_Task(Task_StopTick10, MS2TICK(DEMO_TICK10_RUN_MS), 0);
    }

    /* --- Demo thứ tự: thêm NGƯỢC thời gian (C, B, A) -> phải chạy A, B, C ---
     *     Delay đủ dài để vòng lặp chính được vào trước khi các callback này đến hạn. */
    SCH_Add_Task(OneShot_C, MS2TICK(5000), 0);
    SCH_Add_Task(OneShot_B, MS2TICK(3000), 0);
    SCH_Add_Task(OneShot_A, MS2TICK(1500), 0);

    HAL_TIM_Base_Start_IT(&htim2);       /* bật tick 10 ms SAU khi đã đăng ký xong */
}
