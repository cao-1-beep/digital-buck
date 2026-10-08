/* 使用此外设驱动函数方法 */

/**
  1，在main.c中找到下面第5和第8行的代码，在他们中间复制第6、7行代码，调用头文件
          USER CODE BEGIN Includes
            #include "OLED.h"
            #include "bmp.h"              //图片库，一张图片写成一个函数
          USER CODE END Includes
  
  2，在main.c中，int.main函数下，while循环语句前的BEGIN和END之间写OLED初始化函数
          OLED_Init();                    //OLED初始化函数
  
  3，在main.c中，int.main函数下，OLED初始化函数后面配置OLED显示信息
          OLED_ColorTurn(0);              //背景色  ：0 正常显示，1 反色显示
          OLED_DisplayTurn(0);            //显示方向：0 正常显示，1 屏幕翻转显示
  
  4，OLED屏幕显示函数，以部分显示函数为例，全部显示函数在OLED.h文件中，
      常见尺寸：数字字符大小：8（6x8）/12（6x12）/16（8x16）/24（12x24）
               汉字字库大小：16（16x16）/24（24x24）/32（32x32）/64（64x64）
          eg. OLED_ShowChinese(0,0,2,16,1);               //中文显示函数
                    在（0，0）点坐标开始，显示字库中位号为2的汉字，字体大小16，显示色和背景色相反
                    字库在OLEDFONT.h文件中Hzk1[][32]、Hzk2[][72]、Hzk3[][128]、Hzk4[][512]数组下
                    字库中的字，需要用专用的取模软件生成像素点
                    
              uint8_t hanzi[] = {0, 1, 2, 3, 4, 5, 6, 0xFF};            //定义数组，顺序显示汉字的序号，0、1、2、3、4、5、6，0xFF：结束标志
              OLED_ShowCNString(7, 0, hanzi, 16, 1);                    //OLED屏显示函数，多字中文
                    在（7，0）点坐标开始，顺序显示hanzi数组中的汉字，字体大小16，显示色和背景色相反
                    字库在OLEDFONT.h文件中Hzk1[][32]、Hzk2[][72]、Hzk3[][128]、Hzk4[][512]数组下
                    字库中的字，需要用专用的取模软件生成像素点
                    
              OLED_ShowPicture(10,20,30,40,yijiao[],1);   //图片显示函数
                    在（10，20）点坐标开始，显示图片长度为30、宽度为40，显示yijiao[]的图片，显示色和背景色相反
                    图片像素点写在BMP.h文件中，一个图片对应一个数组
                    图片库中的图片，需要用专用的取模软件生成像素点
                    
              OLED_ShowString(16,24,"ABCD",8,1);           //字符串显示函数
                    在（16，24）点坐标开始，显示字符串ABCD，字体大小为8，显示色和背景色相反
                    
              OLED_ShowChar(111, 16, '%', 16, 1);
                    在（111，16）点坐标开始，显示字符%，字体大小为16，显示色和背景色相反
                    
              OLED_Refresh();                             //只要有OLED显示函数就要OLED屏幕刷新，否则屏幕不会刷新
  
  注意要点
    1，在OLED.h文件下修改屏幕与单片机连接IO口，文件中的第11、12、14、15四行的引脚名称
    2，在OLED.c文件下修改屏幕与单片机连接IO口，在OLED_Init(void)函数里面修改，函数中有备注
    3，屏幕大小为128*64，所以坐标点范围为：x 0~127；y 0~63
  */
#include "OLED.h"
#include "stdlib.h"
#include "OLEDFONT.h"
#include "main.h"

/**
  * @简要     显存，定义屏幕长度的数组，用于存放数据
  *               二位数组：144 屏幕长度
  */
uint8_t OLED_GRAM[128][8];

/**
  * @简要     延时函数
  * @参数     无
  * @返回值   无
  */
void IIC_delay(void)
{
  uint8_t t=3;
  while(t--);
}

/**
  * @简要     起始信号，开始信号传输
  * @参数     无
  * @返回值   无
  */
void I2C_Start(void)
{
  OLED_SDA_Set();
  OLED_SCL_Set();
  IIC_delay();
  OLED_SDA_Clr();
  IIC_delay();
  OLED_SCL_Clr();
  IIC_delay();
}

/**
  * @简要     结束信号，停止信号传输
  * @参数     无
  * @返回值   无
  */
void I2C_Stop(void)
{
  OLED_SDA_Clr();
  OLED_SCL_Set();
  IIC_delay();
  OLED_SDA_Set();
}

/**
  * @简要     等待信号响应
  * @参数     无
  * @返回值   无
  */
void I2C_WaitAck(void) //测数据信号的电平
{
  OLED_SDA_Set();
  IIC_delay();
  OLED_SCL_Set();
  IIC_delay();
  OLED_SCL_Clr();
  IIC_delay();
}

/**
  * @简要     写入一个字节
  * @参数     等待写入的命令或数据，即字库中的像素点，依次写入，等待发送
  * @参数     数据/命令标志，mode：0 表示命令;1 表示数据;
  * @返回值   无
  */
void OLED_WR_Byte(uint8_t dat,uint8_t mode)
{
  I2C_Start();
  Send_Byte(0x78);
  I2C_WaitAck();
  if(mode){Send_Byte(0x40);}
  else{Send_Byte(0x00);}
  I2C_WaitAck();
  Send_Byte(dat);
  I2C_WaitAck();
  I2C_Stop();
}

/**
  * @简要     发送一个字节
  * @参数     需要发送的数据，即字库中的像素点，依次发送
  * @返回值   无
  */
void Send_Byte(uint8_t dat)
{
  uint8_t i;
  for(i=0;i<8;i++)
  {
    if(dat&0x80)//将dat的8位从最高位依次写入
    {
      OLED_SDA_Set();
    }
    else
    {
      OLED_SDA_Clr();
    }
    IIC_delay();
    OLED_SCL_Set();
    IIC_delay();
    OLED_SCL_Clr();//将时钟信号设置为低电平
    dat<<=1;
  }
}

/**
  * @简要     开启OLED显示 
  * @参数     无
  * @返回值   无
  */
void OLED_DisPlay_On(void)
{
  OLED_WR_Byte(0x8D,OLED_CMD);//电荷泵使能
  OLED_WR_Byte(0x14,OLED_CMD);//开启电荷泵
  OLED_WR_Byte(0xAF,OLED_CMD);//点亮屏幕
}

/**
  * @简要     关闭OLED显示 
  * @参数     无
  * @返回值   无
  */
void OLED_DisPlay_Off(void)
{
  OLED_WR_Byte(0x8D,OLED_CMD);//电荷泵使能
  OLED_WR_Byte(0x10,OLED_CMD);//关闭电荷泵
  OLED_WR_Byte(0xAE,OLED_CMD);//关闭屏幕
}

/**
  * @简要     更新显存到OLED，OLED更新显示
  * @参数     无
  * @返回值   无
  */
void OLED_Refresh(void)
{
  uint8_t i,n;
  for(i=0;i<8;i++)
  {
    OLED_WR_Byte(0xb0+i,OLED_CMD); //设置行起始地址
    OLED_WR_Byte(0x00,OLED_CMD);   //设置低列起始地址
    OLED_WR_Byte(0x10,OLED_CMD);   //设置高列起始地址
    I2C_Start();
    Send_Byte(0x78);
    I2C_WaitAck();
    Send_Byte(0x40);
    I2C_WaitAck();
    for(n=0;n<128;n++)
    {
      Send_Byte(OLED_GRAM[n][i]);
      I2C_WaitAck();
    }
    I2C_Stop();
  }
}

/**
  * @简要     OLED全屏清屏函数
  * @参数     无
  * @返回值   无
  */
void OLED_Clear(void)
{
  uint8_t i,n;
  for(i=0;i<8;i++)
  {
     for(n=0;n<128;n++)
      {
       OLED_GRAM[n][i]=0;//清除所有数据
      }
  }
  OLED_Refresh();//更新显示
}

/**
  * @简要     OLED局部清屏函数
  * @参数     清屏起始坐标，x0
  * @参数     清屏起始坐标，y0
  * @参数     清屏结束坐标，x1
  * @参数     清屏结束坐标，y1
  * @返回值   无
  */
void OLED_PartClear(uint8_t x0,uint8_t y0,uint8_t x1,uint8_t y1)
{
  uint8_t i,n;
    for( i=x0;i<x1;i++)
    {
        for( n=y0;n<y1;n++)
        {
            OLED_GRAM[i][n]=0;
        }
    }
    OLED_Refresh();
}

/**
  * @简要     反显函数，背景色设置函数
  * @参数     显示颜色
  *               mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ColorTurn(uint8_t i)
{
  if(i==0)
    {
      OLED_WR_Byte(0xA6,OLED_CMD);//正常显示
    }
  if(i==1)
    {
      OLED_WR_Byte(0xA7,OLED_CMD);//反色显示
    }
}

/**
  * @简要     屏幕旋转180度，屏幕显示方向
  * @参数     显示方向
  *               i：0 正常显示，1 屏幕翻转显示
  * @返回值   无
  */
void OLED_DisplayTurn(uint8_t i)
{
  if(i==0)
    {
      OLED_WR_Byte(0xC8,OLED_CMD);//正常显示
      OLED_WR_Byte(0xA1,OLED_CMD);
    }
  if(i==1)
    {
      OLED_WR_Byte(0xC0,OLED_CMD);//反转显示
      OLED_WR_Byte(0xA0,OLED_CMD);
    }
}

/**
  * @简要     OLED的初始化
  *   @注意   OLED显示屏与单片机的连接引脚在这里修改要修改的地方在函数下已做说明
  * @参数     无
  * @返回值   无
  */
void OLED_Init(void)
{
	__HAL_RCC_GPIOB_CLK_ENABLE();   /* 原文件里这句是注释掉的,补上有效的一句,否则 PB6/PB7 不工作 */

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /*            OLED连接的两个IO口同为PAX或PBX，eg.PA11和PA12，使用以下IO初始化代码            */
    
    __HAL_RCC_GPIOB_CLK_ENABLE();                       /* 本板 OLED 在 PB6(SCL)/PB7(SDA) */
  
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6|GPIO_PIN_7, GPIO_PIN_RESET);  //要改IO口标号
    
    /*Configure GPIO pins : PB6(SCL) PB7(SDA) */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;      //要改IO口标号
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);             //要改IO口标号
    
    /*         OLED连接的两个IO口使用了不同的PAX和PBX，eg.PA0和PB0，使用以下IO初始化代码         */
//    __HAL_RCC_GPIOB_CLK_ENABLE();                       /* 本板 OLED 在 PB6(SCL)/PB7(SDA) */，初始化PA
//  
//    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);   //要改IO口标号
//
//    /*Configure GPIO pins : PA0 */
//    GPIO_InitStruct.Pin = GPIO_PIN_0;                   //要改IO口标号
//    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);             //要改IO口标号

//    __HAL_RCC_GPIOB_CLK_ENABLE();                       //要改IO口标号，初始化PB
//  
//    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);   //要改IO口标号
//
//    /*Configure GPIO pins : PB0 */
//    GPIO_InitStruct.Pin = GPIO_PIN_0;                   //要改IO口标号
//    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);             //要改IO口标号

//  OLED_RES_Clr();    //复位启用
  HAL_Delay(200);
//  OLED_RES_Set();    //复位启用
  
  OLED_WR_Byte(0xAE,OLED_CMD);//--turn off oled panel
  OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
  OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
  OLED_WR_Byte(0x40,OLED_CMD);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
  OLED_WR_Byte(0x81,OLED_CMD);//--set contrast control register
  OLED_WR_Byte(0xCF,OLED_CMD);// Set SEG Output Current Brightness
  OLED_WR_Byte(0xA1,OLED_CMD);//--Set SEG/Column Mapping     0xa0左右反置 0xa1正常
  OLED_WR_Byte(0xC8,OLED_CMD);//Set COM/Row Scan Direction   0xc0上下反置 0xc8正常
  OLED_WR_Byte(0xA6,OLED_CMD);//--set normal display
  OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
  OLED_WR_Byte(0x3f,OLED_CMD);//--1/64 duty
  OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset  Shift Mapping RAM Counter (0x00~0x3F)
  OLED_WR_Byte(0x00,OLED_CMD);//-not offset
  OLED_WR_Byte(0xd5,OLED_CMD);//--set display clock divide ratio/oscillator frequency
  OLED_WR_Byte(0x80,OLED_CMD);//--set divide ratio, Set Clock as 100 Frames/Sec
  OLED_WR_Byte(0xD9,OLED_CMD);//--set pre-charge period
  OLED_WR_Byte(0xF1,OLED_CMD);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
  OLED_WR_Byte(0xDA,OLED_CMD);//--set com pins hardware configuration
  OLED_WR_Byte(0x12,OLED_CMD);
  OLED_WR_Byte(0xDB,OLED_CMD);//--set vcomh
  OLED_WR_Byte(0x30,OLED_CMD);//Set VCOM Deselect Level
  OLED_WR_Byte(0x20,OLED_CMD);//-Set Page Addressing Mode (0x00/0x01/0x02)
  OLED_WR_Byte(0x02,OLED_CMD);//
  OLED_WR_Byte(0x8D,OLED_CMD);//--set Charge Pump enable/disable
  OLED_WR_Byte(0x14,OLED_CMD);//--set(0x10) disable
  OLED_Clear();
  OLED_WR_Byte(0xAF,OLED_CMD);
}

/*                                  显示函数                                  */

/**
  * @简要     画点
  * @参数     点坐标，x：0~127
  * @参数     点坐标，y：0~63
  * @参数     是否绘制实心点，mode：0 清空，1 填充
  *             其实和显示颜色一样的效果，mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t mode)
{
  uint8_t i,m,n;
  i=y/8;
  m=y%8;
  n=1<<m;
  if(mode){OLED_GRAM[x][i]|=n;}
  else
  {
    OLED_GRAM[x][i]=~OLED_GRAM[x][i];
    OLED_GRAM[x][i]|=n;
    OLED_GRAM[x][i]=~OLED_GRAM[x][i];
  }
}

/**
  * @简要     画圆
  * @参数     圆心坐标，x：0~127
  * @参数     圆心坐标，y：0~63
  * @参数     圆的半径，r
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r,uint8_t mode)
{
  int a, b,num;
    a = 0;
    b = r;
    while(2 * b * b >= r * r)      
    {
        OLED_DrawPoint(x + a, y - b,mode);
        OLED_DrawPoint(x - a, y - b,mode);
        OLED_DrawPoint(x - a, y + b,mode);
        OLED_DrawPoint(x + a, y + b,mode);
 
        OLED_DrawPoint(x + b, y + a,mode);
        OLED_DrawPoint(x + b, y - a,mode);
        OLED_DrawPoint(x - b, y - a,mode);
        OLED_DrawPoint(x - b, y + a,mode);
        
        a++;
        num = (a * a + b * b) - r*r;//计算画的点离圆心的距离
        if(num > 0)
        {
            b--;
            a--;
        }
    }
}

/**
  * @简要     画线
  * @参数     起点坐标，x1
  * @参数     起点坐标，y1
  * @参数     终点坐标，x2
  * @参数     终点坐标，y2
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode)
{
  uint16_t t; 
  int xerr=0,yerr=0,delta_x,delta_y,distance;
  int incx,incy,uRow,uCol;
  delta_x=x2-x1;                        //计算坐标增量 
  delta_y=y2-y1;
  
  uRow=x1;                              //画线起点坐标
  uCol=y1;
  
  if(delta_x>0)incx=1;                  //设置单步方向 
  else if (delta_x==0)incx=0;           //垂直线 
  else {incx=-1;delta_x=-delta_x;}
  if(delta_y>0)incy=1;
  else if (delta_y==0)incy=0;           //水平线 
  else {incy=-1;delta_y=-delta_x;}
  if(delta_x>delta_y)distance=delta_x;  //选取基本增量坐标轴 
  else distance=delta_y;
  
  for(t=0;t<distance+1;t++)
  {
    OLED_DrawPoint(uRow,uCol,mode);     //画点
    xerr+=delta_x;
    yerr+=delta_y;
    if(xerr>distance)
    {
      xerr-=distance;
      uRow+=incx;
    }
    if(yerr>distance)
    {
      yerr-=distance;
      uCol+=incy;
    }
  }
}

/**
  * @简要     画方框
  * @参数     起点坐标，x1
  * @参数     起点坐标，y1
  * @参数     终点坐标，x2
  * @参数     终点坐标，y2
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_Square(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode)
{
  OLED_DrawLine(x1,y1,x2,y1,mode);
  OLED_DrawLine(x2,y1,x2,y2,mode);  
  OLED_DrawLine(x1,y2,x2,y2,mode);  
  OLED_DrawLine(x1,y1,x1,y2,mode);
}

/**
  * @简要     画4*4正方形矩阵
  *   @注意   函数中的if判断语句中的x<5，y<5改成x<9，y<11，则可以绘制8*10矩阵
  * @参数     起点坐标，x
  * @参数     起点坐标，y
  * @返回值   无
  */
void OLED_juzhen(uint8_t x1,uint8_t y1)
{
  uint8_t x,y;
  for(x=1;x<5;x++)                  //x<a，矩阵长度为a-1，可修改
  {
    for(y=1;y<5;y++)                //y<b，矩阵宽度为b-1，可修改
    {
      OLED_Square(x*8+x1,y*8+y1,x*8+x1+8,y*8+y1+8,1);
    }
  }
}

/**
  * @简要     单字符显示，在指定位置显示一个字符,包括部分字符
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示字符
  * @参数     字体大小，size1：8（6x8）/12（6x12）/16（8x16）/24（12x24）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size1,uint8_t mode)
{
  uint8_t i,m,temp,size2,chr1;
  uint8_t x0=x,y0=y;
  if(size1==8)size2=6;
  else size2=(size1/8+((size1%8)?1:0))*(size1/2);  //得到字体一个字符对应点阵集所占的字节数
  chr1=chr-' ';  //计算偏移后的值
  for(i=0;i<size2;i++)
  {
    if(size1==8)
        {temp=asc2_0806[chr1][i];} //调用0806字体
    else if(size1==12)
        {temp=asc2_1206[chr1][i];} //调用1206字体
    else if(size1==16)
        {temp=asc2_1608[chr1][i];} //调用1608字体
    else if(size1==24)
        {temp=asc2_2412[chr1][i];} //调用2412字体
    else return;
    for(m=0;m<8;m++)
    {
      if(temp&0x01)OLED_DrawPoint(x,y,mode);
      else OLED_DrawPoint(x,y,!mode);
      temp>>=1;
      y++;
    }
    x++;
    if((size1!=8)&&((x-x0)==size1/2))
    {x=x0;y0=y0+8;}
    y=y0;
  }
}

/**
  * @简要     多字符显示，显示字符串
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示字符串
  * @参数     字体大小，size1：8（6x8）/12（6x12）/16（8x16）/24（12x24）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *chr,uint8_t size1,uint8_t mode)
{
  while((*chr>=' ')&&(*chr<='~'))//判断是不是非法字符!
  {
    OLED_ShowChar(x,y,*chr,size1,mode);
    if(size1==8)x+=6;
    else x+=size1/2;
    chr++;
  }
}

/**
  * @简要     幂指函数，求幂m^n
  * @参数     底数，m
  * @参数     指数，n
  * @返回值   m^n的结果
  */
uint32_t OLED_Pow(uint8_t m,uint8_t n)
{
  uint32_t result=1;
  while(n--)
  {
    result*=m;
  }
  return result;
}

/**
  * @简要     显示整数
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示整数，num
  * @参数     数字的长度，len 
  * @参数     字体大小，size1：8（6x8）/12（6x12）/16（8x16）/24（12x24）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size1,uint8_t mode)
{
  uint8_t t,temp,m=0;
  if(size1==8)m=2;
  for(t=0;t<len;t++)
  {
    temp=(num/OLED_Pow(10,len-t-1))%10;
      if(temp==0)
      {
        OLED_ShowChar(x+(size1/2+m)*t,y,'0',size1,mode);
      }
      else 
      {
        OLED_ShowChar(x+(size1/2+m)*t,y,temp+'0',size1,mode);
      }
  }
}

/**
  * @简要     显示小数
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示小数，num
  * @参数     数字整数的长度，z_len 
  * @参数     数字小数的长度，f_len 
  * @参数     字体大小，size1：8（6x8）/12（6x12）/16（8x16）/24（12x24）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_Showdecimal(uint8_t x,uint8_t y,float num,uint8_t z_len,uint8_t f_len,uint8_t size1,uint8_t mode)
{
  uint8_t t,temp;
  uint8_t enshow=0;
  int z_temp,f_temp;
  z_temp=(int)num;
  for(t=0;t<z_len;t++)
  {
    temp=(z_temp/OLED_Pow(10,z_len-t-1))%10;
    if(enshow==0 && t<(z_len-1))
    {
      if(temp==0)
      {
        OLED_ShowChar(x+(size1/2)*t,y,' ',size1,mode);
        continue;
      }
      else
        enshow=1;
    }
    OLED_ShowChar(x+(size1/2)*t,y,temp+'0',size1,mode);
  }

  OLED_ShowChar(x+(size1/2)*(z_len),y,'.',size1,mode);

  f_temp=(int)((num-z_temp)*(OLED_Pow(10,f_len)));

  for(t=0;t<f_len;t++)
  {
    temp=(f_temp/OLED_Pow(10,f_len-t-1))%10;
    OLED_ShowChar(x+(size1/2)*(t+z_len)+5,y,temp+'0',size1,mode); 
  }
}

/**
  * @简要     显示汉字
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示汉字在字库中的序号，num
  * @参数     字体大小，size1：16（16x16）/24（24x24）/32（32x32）/64（64x64）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowChinese(uint8_t x,uint8_t y,uint8_t num,uint8_t size1,uint8_t mode)
{
  uint8_t m,temp;
  uint8_t x0=x,y0=y;
  uint16_t i,size3=(size1/8+((size1%8)?1:0))*size1;  //得到字体一个字符对应点阵集所占的字节数
  for(i=0;i<size3;i++)
  {
    if(size1==16)
        {temp=Hzk1[num][i];}//调用16*16字体
    else if(size1==24)
        {temp=Hzk2[num][i];}//调用24*24字体
    else if(size1==32)       
        {temp=Hzk3[num][i];}//调用32*32字体
    else if(size1==64)
        {temp=Hzk4[num][i];}//调用64*64字体
    else return;
    for(m=0;m<8;m++)
    {
      if(temp&0x01)OLED_DrawPoint(x,y,mode);
      else OLED_DrawPoint(x,y,!mode);
      temp>>=1;
      y++;
    }
    x++;
    if((x-x0)==size1)
    {x=x0;y0=y0+8;}
    y=y0;
  }
}

/**
  * @简要     显示汉字串，多个汉字
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示汉字串对应的数组
  * @参数     字体大小，size1：16（16x16）/24（24x24）/32（32x32）/64（64x64）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowCNString(uint8_t x,uint8_t y,uint8_t *str,uint8_t size1,uint8_t mode)
{
    uint16_t i=0;
    uint8_t px = x;

    // 循环显示直到结束 0xFF
    while(str[i] != 0xFF)
    {
        // 直接调用你原来的单汉字函数（正确！）
        OLED_ShowChinese(px, y, str[i], size1, mode);

        px += size1;   // 汉字宽度偏移
        i++;            // 下一个
    }
}

//num 显示汉字的个数
//space 每一遍显示的间隔
//mode:0,反色显示;1,正常显示
void OLED_ScrollDisplay(uint8_t num,uint8_t space,uint8_t mode)
{
  uint8_t i,n,t=0,m=0,r;
  while(1)
  {
    if(m==0)
    {
      OLED_ShowChinese(128,24,t,16,mode); //写入一个汉字保存在OLED_GRAM[][]数组中
      t++;
    }
    if(t==num)
      {
        for(r=0;r<16*space;r++)      //显示间隔
         {
          for(i=1;i<144;i++)
            {
              for(n=0;n<8;n++)
              {
                OLED_GRAM[i-1][n]=OLED_GRAM[i][n];
              }
            }
           OLED_Refresh();
         }
        t=0;
      }
    m++;
    if(m==16){m=0;}
    for(i=1;i<144;i++)   //实现左移
    {
      for(n=0;n<8;n++)
      {
        OLED_GRAM[i-1][n]=OLED_GRAM[i][n];
      }
    }
    OLED_Refresh();
  }
}

/**
  * @简要     显示图片
  * @参数     起点坐标，x：0~127
  * @参数     起点坐标，y：0~63
  * @参数     待显示图片的长度，sizex
  * @参数     待显示图片的宽度，sizey
  * @参数     待显示图片对应的数组，图片数组定义都在BMP.h文件中
  * @参数     字体大小，size1：8（6x8）/12（6x12）/16（8x16）/24（12x24）
  * @参数     mode：0 正常显示，1 反色显示
  * @返回值   无
  */
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode)
{
  uint16_t j=0;
  uint8_t i,n,temp,m;
  uint8_t x0=x,y0=y;
  sizey=sizey/8+((sizey%8)?1:0);
  for(n=0;n<sizey;n++)
  {
     for(i=0;i<sizex;i++)
     {
        temp=BMP[j];
        j++;
        for(m=0;m<8;m++)
        {
          if(temp&0x01)OLED_DrawPoint(x,y,mode);
          else OLED_DrawPoint(x,y,!mode);
          temp>>=1;
          y++;
        }
        x++;
        if((x-x0)==sizex)
        {
          x=x0;
          y0=y0+8;
        }
        y=y0;
     }
   }
}


