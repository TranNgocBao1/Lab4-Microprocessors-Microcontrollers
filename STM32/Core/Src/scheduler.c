#include "scheduler.h"
#include "main.h"          /* kéo theo CMSIS: __disable_irq, __get_PRIMASK, __WFI, ... */

/*
 * Cooperative scheduler dạng DELTA-LIST.
 * Các task nằm trong danh sách liên kết sắp theo thời điểm đến hạn;
 * mỗi node chỉ lưu độ chênh (delta) so với node đứng trước.
 *   -> SCH_Update chỉ trừ 1 ở node đầu: O(1), không duyệt mảng.
 *   -> Danh sách luôn đúng thứ tự thời gian: callback gọi đúng thứ tự.
 * Việc tìm vị trí chèn (O(n)) chỉ xảy ra ở main (Add_Task / chèn lại task chu kỳ).
 */

static sTask pool[SCH_MAX_TASKS];
static int8_t head = -1;                     /* -1 = danh sách rỗng */
static uint32_t newTaskID = 0;
static volatile uint32_t sys_ticks = 0;

volatile uint8_t Error_code_G = 0;

static uint32_t Get_New_Task_ID(void) {
    newTaskID++;
    if (newTaskID == NO_TASK_ID) newTaskID++;    /* bỏ qua 0 khi tràn */
    return newTaskID;
}

/* Chèn node idx vào danh sách, đến hạn sau 'delay' tick tính từ BÂY GIỜ.
 * Gọi khi đã tắt ngắt. */
static void insert_sorted(int8_t idx, uint32_t delay) {
    int8_t prev = -1, cur = head;
    while (cur != -1 && delay >= pool[cur].Delay) {   /* '>=' : cùng thời điểm thì vào sau (FIFO) */
        delay -= pool[cur].Delay;
        prev = cur;
        cur = pool[cur].next;
    }
    pool[idx].Delay = delay;
    pool[idx].next  = cur;
    if (cur != -1) pool[cur].Delay -= delay;          /* node phía sau giảm delta tương ứng */
    if (prev == -1) head = idx; else pool[prev].next = idx;
}

void SCH_Init(void) {
    for (uint8_t i = 0; i < SCH_MAX_TASKS; i++) {
        pool[i].pTask  = 0;
        pool[i].Delay  = 0;
        pool[i].Period = 0;
        pool[i].TaskID = NO_TASK_ID;
        pool[i].next   = -1;
    }
    head = -1;
    sys_ticks = 0;
    Error_code_G = 0;
}

/* Gọi trong ISR timer mỗi TICK_MS. O(1). */
void SCH_Update(void) {
    sys_ticks++;
    if (head != -1 && pool[head].Delay > 0) pool[head].Delay--;
}

uint32_t SCH_Add_Task(void (*pFunction)(void), uint32_t DELAY, uint32_t PERIOD) {
    if (pFunction == 0) {
        Error_code_G = ERROR_SCH_NULL_TASK;
        return NO_TASK_ID;
    }

    int8_t idx = -1;
    for (uint8_t i = 0; i < SCH_MAX_TASKS; i++) {
        if (pool[i].pTask == 0) { idx = (int8_t)i; break; }
    }
    if (idx < 0) {                                    /* hết chỗ */
        Error_code_G = ERROR_SCH_TOO_MANY_TASKS;
        return NO_TASK_ID;
    }

    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    pool[idx].pTask  = pFunction;
    pool[idx].Period = PERIOD;
    pool[idx].TaskID = Get_New_Task_ID();
    uint32_t id = pool[idx].TaskID;
    insert_sorted(idx, DELAY);
    __set_PRIMASK(pm);
    return id;
}

uint8_t SCH_Delete_Task(uint32_t taskID) {
    if (taskID == NO_TASK_ID) {
        Error_code_G = ERROR_SCH_CANNOT_DELETE_TASK;
        return RETURN_ERROR;
    }

    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    int8_t prev = -1, cur = head;
    while (cur != -1 && pool[cur].TaskID != taskID) {
        prev = cur;
        cur = pool[cur].next;
    }
    if (cur == -1) {                                  /* không tìm thấy */
        __set_PRIMASK(pm);
        Error_code_G = ERROR_SCH_CANNOT_DELETE_TASK;
        return RETURN_ERROR;
    }

    if (pool[cur].next != -1) pool[pool[cur].next].Delay += pool[cur].Delay;  /* trả delta cho node sau */
    if (prev == -1) head = pool[cur].next; else pool[prev].next = pool[cur].next;

    pool[cur].pTask  = 0;
    pool[cur].Delay  = 0;
    pool[cur].Period = 0;
    pool[cur].TaskID = NO_TASK_ID;
    pool[cur].next   = -1;
    __set_PRIMASK(pm);
    return RETURN_NORMAL;
}

/* Gọi trong while(1). Chạy mọi task đã đến hạn theo đúng thứ tự. */
void SCH_Dispatch_Tasks(void) {
    for (;;) {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        if (head == -1 || pool[head].Delay != 0) {    /* chưa có task nào đến hạn */
            __set_PRIMASK(pm);
            break;
        }
        int8_t idx = head;
        head = pool[idx].next;                         /* lấy node đầu ra khỏi danh sách */
        void (*fn)(void) = pool[idx].pTask;

        if (pool[idx].Period > 0) {
            insert_sorted(idx, pool[idx].Period);      /* task chu kỳ: chèn lại TRƯỚC khi chạy */
        } else {                                       /* one-shot: giải phóng node */
            pool[idx].pTask  = 0;
            pool[idx].TaskID = NO_TASK_ID;
            pool[idx].next   = -1;
        }
        __set_PRIMASK(pm);

        fn();                                          /* chạy task khi ngắt đã bật lại */
    }
    SCH_Go_To_Sleep();
}

void SCH_Go_To_Sleep(void) {
#if SCH_USE_SLEEP
    __WFI();                                           /* ngủ tới ngắt kế tiếp (tick) */
#endif
}

uint32_t get_time(void) {
    return sys_ticks * TICK_MS;
}
