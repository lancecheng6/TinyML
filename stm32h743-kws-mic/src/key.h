// key.h — KEY1 = PA0 (Wildfire H7Pro), active HIGH (external pull-down)
#ifndef H743_KEY_H
#define H743_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

void Key_Init(void);
// Non-blocking. Returns 1 once per valid press+release cycle
// (press must stay stable for 50 ms; trigger fires on release).
int Key1_Poll(void);

#ifdef __cplusplus
}
#endif

#endif
