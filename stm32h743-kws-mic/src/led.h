// led.h — D6 RGB LED (Wildfire H7Pro): R=PB0, G=PB1, B=PA3, active LOW
#ifndef H743_LED_H
#define H743_LED_H

#ifdef __cplusplus
extern "C" {
#endif

void Led_Init(void);
void Led_Red(int on);
void Led_Green(int on);
void Led_Blue(int on);

#ifdef __cplusplus
}
#endif

#endif
