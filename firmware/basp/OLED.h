#ifndef __OLED_H
#define __OLED_H 

#include "main.h"
#include "stdlib.h"  

/**
  *   4引脚OLED有两个信号线需要与单片机IO口连接，SCL时钟线和SDA数据线
  *       需要改第11、12、14、15行四行的IO口编号
  */
#define OLED_SCL_Clr() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_RESET);      //SCL引脚PB6接口低电平，返回0
#define OLED_SCL_Set() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_6,GPIO_PIN_SET);        //SCL引脚PB6接口高电平，返回1

#define OLED_SDA_Clr() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_RESET);      //SDA引脚PB7接口低电平，返回0
#define OLED_SDA_Set() HAL_GPIO_WritePin(GPIOB,GPIO_PIN_7,GPIO_PIN_SET);        //SDA引脚PB7接口高电平，返回1

#define OLED_CMD  0  //写命令
#define OLED_DATA 1  //写数据

void IIC_delay(void);                                               //延时函数
void I2C_Start(void);                                               //I2C操作函数，协议开
void I2C_Stop(void);                                                //I2C操作函数，协议关
void I2C_WaitAck(void);                                             //I2C操作函数，等待信号响应
void OLED_WR_Byte(uint8_t dat,uint8_t mode);                        //OLED操作函数，写入一个字节
void Send_Byte(uint8_t dat);                                        //OLED操作函数，发送一个字节
void OLED_DisPlay_On(void);                                         //OLED操作函数，OLED显示开
void OLED_DisPlay_Off(void);                                        //OLED操作函数，OLED显示关
void OLED_Refresh(void);                                            //OLED操作函数，OLED更新显示
void OLED_Clear(void);                                              //OLED操作函数，OLED全屏清屏
void OLED_PartClear(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1);   //OLED操作函数，OLED部分清屏

void OLED_ColorTurn(uint8_t i);                                     //反显函数，0正常显示，1 反色显示，OLED背景色设置
void OLED_DisplayTurn(uint8_t i);                                   //显示方向，0正常显示，1 屏幕翻转显示
void OLED_Init(void);                                               //OLED初始化函数

uint32_t OLED_Pow(uint8_t m,uint8_t n);                             //幂指函数

void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t mode);                                              //OLED屏显示函数，点
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r,uint8_t mode);                                   //OLED屏显示函数，圆
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode);                       //OLED屏显示函数，线
void OLED_Square(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode);                         //画方框
void OLED_juzhen(uint8_t x1,uint8_t y1);                                                            //画4*4正方形矩阵

void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size1,uint8_t mode);                     //OLED屏显示函数，单字符
void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *chr,uint8_t size1,uint8_t mode);                  //OLED屏显示函数，字符串

/*                                OLED屏显示函数，十进制数字，整数                                */
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size1,uint8_t mode);
/*                                OLED屏显示函数，十进制数字，小数                                */
void OLED_Showdecimal(uint8_t x,uint8_t y,float num,uint8_t z_len,uint8_t f_len,uint8_t size1,uint8_t mode);


void OLED_ShowChinese(uint8_t x,uint8_t y,uint8_t num,uint8_t size1,uint8_t mode);                  //OLED屏显示函数，单字中文
void OLED_ShowCNString(uint8_t x,uint8_t y,uint8_t *str,uint8_t size1,uint8_t mode);                //OLED屏显示函数，多字中文

void OLED_ScrollDisplay(uint8_t num,uint8_t space,uint8_t mode);                                    //OLED屏显示函数，显示方式
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode);  //OLED屏显示函数，图片

#endif
