/*
 * scheduler.h
 *
 *  Created on: Nov 11, 2026
 *      Author: baoba
 */

#ifndef INC_SCHEDULER_H_
#define INC_SCHEDULER_H_


#include <stdint.h>

/* ===== Cấu hình ===== */
#define SCH_MAX_TASKS   16   /* số node tối đa: task đang chờ + task one-shot sinh ra lúc chạy */
#define TICK_MS         10   /* chu kỳ ngắt timer (ms); phải khớp PSC/ARR của TIM2 */

#ifndef SCH_USE_SLEEP
#define SCH_USE_SLEEP   0    /* 1 = cho CPU ngủ (WFI) sau mỗi lượt dispatch (tùy chọn, mục 2.3.8) */
#endif

/* ===== Giá trị trả về ===== */
#define NO_TASK_ID      0    /* ID 0 = không hợp lệ / báo lỗi */
#define RETURN_NORMAL   0
#define RETURN_ERROR    1

/* ===== Mã lỗi ghi vào Error_code_G (mục 2.3.9) ===== */
#define ERROR_SCH_TOO_MANY_TASKS      1
#define ERROR_SCH_CANNOT_DELETE_TASK  2
#define ERROR_SCH_NULL_TASK           3

typedef struct {
    void (*pTask)(void);   /* hàm cần chạy (void f(void)) */
    uint32_t Delay;        /* DELTA (tick) so với task đứng trước trong danh sách */
    uint32_t Period;       /* chu kỳ lặp (tick); 0 = chạy một lần (one-shot) */
    uint32_t TaskID;       /* định danh duy nhất */
    int8_t   next;         /* chỉ số node kế tiếp trong danh sách, -1 = hết */
} sTask;

extern volatile uint8_t Error_code_G;

void     SCH_Init(void);
void     SCH_Update(void);                 /* gọi trong ISR timer, O(1) */
void     SCH_Dispatch_Tasks(void);         /* gọi trong while(1) */
uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY, uint32_t PERIOD);
uint8_t  SCH_Delete_Task(uint32_t taskID);
void     SCH_Go_To_Sleep(void);            /* tùy chọn */
uint32_t get_time(void);                   /* thời gian hệ thống (ms), bội số của TICK_MS */


#endif /* INC_SCHEDULER_H_ */
