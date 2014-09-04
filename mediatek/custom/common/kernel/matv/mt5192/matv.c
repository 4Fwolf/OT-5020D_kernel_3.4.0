#if 0
#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/spinlock.h>
#include <mach/mt6516_typedefs.h>
#include <linux/interrupt.h>
#include <linux/list.h>
//#include "matv6326_hw.h"

//#include "matv6326_sw.h"
//#include "mt5192MATV_sw.h" // 20100319
#include <mach/mt6516_gpio.h>
#include <linux/kthread.h>
#include <linux/wakelock.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include <cust_eint.h>

#include <linux/delay.h>
#include <linux/platform_device.h>
#else
#include "matv.h"
#endif

#define _NEW_I2C_DRV_

#define _MATV_HIGH_SPEED_

//#define _MATV_HIGH_SPEED_DMA_


static struct class *matv_class = NULL;
static int matv_major = 0;
static dev_t matv_devno;
static struct cdev *matv_cdev;

#ifdef MT6575
static struct semaphore g_mATVLock;
#define matv_lock mutex_lock(&g_mATVLock)
#define matv_unlock mutex_unlock(&g_mATVLock)
#define matv_lock_init init_MUTEX(&g_mATVLock)

#else
static spinlock_t g_mATVLock;
#define matv_lock spin_lock(&g_mATVLock)
#define matv_unlock spin_unlock(&g_mATVLock)
#define matv_lock_init spin_lock_init(&g_mATVLock)

#endif
static int g_mATVCnt =0;
static int g_mI2CErrCnt =0;
#define MAX_I2CERR  10

#ifdef _MATV_HIGH_SPEED_DMA_
static u8 *gpDMABuf_va = NULL;
static u32 gpDMABuf_pa = NULL;
#endif

int matv_in_data[2] = {1,1};
int matv_out_data[2] = {1,1};
int matv_lcdbk_data[1] = {1};


#define mt5192_SLAVE_ADDR_WRITE	0x82
#define mt5192_SLAVE_ADDR_Read	0x83
#define mt5192_SLAVE_ADDR_FW_Update 0xfa

static struct i2c_client *new_client = NULL;

#ifdef _NEW_I2C_DRV_
static const struct i2c_device_id my_i2c_id[] = {{MATV_I2C_DEVNAME,0},{}};   
static unsigned short force[] = {MATV_I2C_CHANNEL, mt5192_SLAVE_ADDR_WRITE, I2C_CLIENT_END, I2C_CLIENT_END};   
static const unsigned short * const forces[] = { force, NULL };              
static struct i2c_client_address_data addr_data = { .forces = forces,};
#else
/* Addresses to scan */
static unsigned short normal_i2c[] = { mt5192_SLAVE_ADDR_WRITE,  I2C_CLIENT_END };
static unsigned short ignore = I2C_CLIENT_END;

static struct i2c_client_address_data addr_data = {
	.normal_i2c = normal_i2c,
	.probe	= &ignore,
	.ignore	= &ignore,
};


static int mt5192_attach_adapter(struct i2c_adapter *adapter);
static int mt5192_detect(struct i2c_adapter *adapter, int address, int kind);
static int mt5192_detach_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver mt5192_driver = {
	.attach_adapter	= mt5192_attach_adapter,
	.detach_client	= mt5192_detach_client,
	.driver = 	{
	    .name		    = "mt5192",
	},
};
#endif

//For TP Mode Settings
extern void tpd_switch_single_mode(void);
extern void tpd_switch_multiple_mode(void); 
extern void tpd_switch_sleep_mode(void);
extern void tpd_switch_normal_mode(void);


ssize_t mt5192_read_byte(u8 cmd, u8 *returnData)
{
    char     cmd_buf[1]={0x00};
    char     readData = 0;
    int     ret=0;

	if(g_mI2CErrCnt> MAX_I2CERR)
	{
		MATV_LOGE("[Error]MATV too many error!! %d\n", g_mI2CErrCnt);
		return 0;
	}
	
    cmd_buf[0] = cmd;
    ret = i2c_master_send(new_client, &cmd_buf[0], 1);
    if (ret < 0) {
        MATV_LOGE("[Error]MATV sends command error!! \n");
		g_mI2CErrCnt++;
        return 0;
    }
    ret = i2c_master_recv(new_client, &readData, 1);
    if (ret < 0) {
        MATV_LOGE("[Error]MATV reads data error!! \n");
		g_mI2CErrCnt++;
        return 0;
    } 
    //MATV_LOGD("func mt5192_read_byte : 0x%x \n", readData);
    *returnData = readData;
    
    return 1;
}

ssize_t mt5192_write_byte(u8 cmd, u8 writeData)
{
    char    write_data[2] = {0};
    int    ret=0;
    
    write_data[0] = cmd;         // ex. 0x81
    write_data[1] = writeData;// ex. 0x44
    if(g_mI2CErrCnt> MAX_I2CERR)
	{
		MATV_LOGE("[Error]MATV too many error!! %d\n", g_mI2CErrCnt);
		return 0;
	}
    
    ret = i2c_master_send(new_client, write_data, 2);
    if (ret < 0) {
        MATV_LOGE("[Error]sends command error!! \n");
		g_mI2CErrCnt++;
        return 0;
    }
    
    return 1;
}

ssize_t mt5192_read_m_byte(u8 cmd, u8 *returnData,U16 len, u8 bAutoInc)
{
    char     cmd_buf[1]={0x00};
    int     ret=0;
    if(len == 0) {
        MATV_LOGE("[Error]MATV Read Len should not be zero!! \n");
        return 0;
    }
	
	if(g_mI2CErrCnt> MAX_I2CERR)
	{
		MATV_LOGE("[Error]MATV too many error!! %d\n", g_mI2CErrCnt);
		return 0;
	}        

    cmd_buf[0] = cmd;
    ret = i2c_master_send(new_client, &cmd_buf[0], 1);
    //MATV_LOGD("[MATV_R]I2C send Size = %d\n",ret);
    if (ret < 0) {
        MATV_LOGE("[Error]MATV sends command error!! \n");
		g_mI2CErrCnt++;
        return 0;
    }
    if ((bAutoInc>0)&(len>=256)) {
        MATV_LOGE("[Error][MATV]Exceeds Maximum read size 256!\n");
        return 0;
    }

    //Driver does not allow len>8
    while(len > 8)
    {
        //MATV_LOGD("[MATV]Remain size = %d\n",len);
        ret = i2c_master_recv(new_client, returnData, 8);
        //MATV_LOGD("[MATV_R]I2C recv Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
			g_mI2CErrCnt++;			
            return 0;
        }
        returnData+=8;
        len -= 8;
        if (bAutoInc){
            cmd_buf[0] = cmd_buf[0]+8;
        }
        ret = i2c_master_send(new_client, &cmd_buf[0], 1);
        //MATV_LOGD("[MATV_R]I2C send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV sends command error!! \n");
			g_mI2CErrCnt++;
            return 0;
        }           
    }
    if (len > 0){
        ret = i2c_master_recv(new_client, returnData, len);
        //MATV_LOGD("[MATV]I2C Read Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
			g_mI2CErrCnt++;
            return 0;
        }
    }

        
    return 1;
}

ssize_t mt5192_write_m_byte(u8 cmd, u8 *writeData,U16 len, u8 bAutoInc)
{
    char    write_data[8] = {0};
    int    i,ret=0;

    if(len == 0) {
        MATV_LOGE("[Error]MATV Write Len should not be zero!! \n");
        return 0;
    }
        
    write_data[0] = cmd;
	if(g_mI2CErrCnt> MAX_I2CERR)
	{
		MATV_LOGE("[Error]MATV too many error!! %d\n", g_mI2CErrCnt);
		return 0;
	}

    //Driver does not allow (single write length > 8)
    while(len > 7)
    {
        for (i = 0; i<7; i++){
            write_data[i+1] = *(writeData+i);    
        }
        ret = i2c_master_send(new_client, write_data, 7+1);
        //MATV_LOGD("[MATV_R]I2C Send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
			g_mI2CErrCnt++;
            return 0;
        }
        writeData+=7;
        len -= 7;
        if (bAutoInc){
            write_data[0] = write_data[0]+7;
        }   
    }
    if (len > 0){
        for (i = 0; i<len; i++){
            write_data[i+1] = *(writeData+i);    
        }
        ret = i2c_master_send(new_client, write_data, len+1);
        //MATV_LOGD("[MATV]I2C Send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
			g_mI2CErrCnt++;
            return 0;
        }
    }

    return 1;
}

#ifdef _MATV_HIGH_SPEED_DMA_

ssize_t mt5192_dma_read_m_byte(u8 cmd, u8 *returnData_va, u32 returnData_pa,U16 len, u8 bAutoInc)
{   
    char     readData = 0;
    int     ret=0, read_count = 0, read_length = 0;
    int    i, total_count = len;
    if(len == 0) {
        MATV_LOGE("[Error]MATV Read Len should not be zero!! \n");
        return 0;
    }
    //MATV_LOGD("mt5192_dma_read_m_byte, cmd=%x, va=%x, pa=%x\n",cmd, returnData_va,returnData_pa); 
    returnData_va[0] = cmd;//use as buffer
    ret = i2c_master_send(new_client, returnData_pa, 1);
    //MATV_LOGD("[MATV_R]I2C send Size = %d\n",ret);
    if (ret < 0) {
        MATV_LOGE("[Error]MATV sends command error!! \n");
        return 0;
    }
    if ((bAutoInc>0)&(len>=256)) {
        MATV_LOGE("[Error][MATV]Exceeds Maximum read size 256!\n");
        return 0;
    }

    while(len > 255)
    {
        read_length = 255;
        ret = i2c_master_recv(new_client, returnData_pa+read_count, read_length+1);
        //MATV_LOGD("[MATV_R]I2C Send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
            return 0;
        }
        read_count+=read_length;
        len -= read_length;
        if (bAutoInc){
            returnData_va[read_count] = cmd + read_count;
        }        
        else{
            returnData_va[read_count] = cmd;
        }
        ret = i2c_master_send(new_client, returnData_pa+read_count, 1);
        //MATV_LOGD("[MATV_R]I2C send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV sends command error!! \n");
            return 0;
        } 
    }
    if (len > 0){
        ret = i2c_master_recv(new_client, returnData_pa+read_count, len+1);
        //MATV_LOGD("[MATV]I2C Read Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
            return 0;
        }
    }

    //for (i = 0; i< total_count; i++)
    //    MATV_LOGD("[MATV]I2C ReadData[%d] = %x\n",i,returnData_va[i]);
       
    return 1;

}

ssize_t mt5192_dma_write_m_byte(u8 cmd, u8 *writeData_va, u32 writeData_pa,U16 len, u8 bAutoInc)
{
    char    write_data[8] = {0};
    int    i,ret=0, write_count = 0, write_length = 0;
    int    total_count = len;
    //new_client->addr = new_client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
    if(len == 0) {
        MATV_LOGE("[Error]MATV Write Len should not be zero!! \n");
        return 0;
    }
    MATV_LOGD("mt5192_dma_write_m_byte,cmd = %x, va=%x, pa=%x, len = %x\n",cmd,writeData_va,writeData_pa,len);     
    writeData_va[write_count] = cmd + write_count;
    //for (i = 0; i< total_count+1; i++)
    //    MATV_LOGD("[MATV]I2C WriteData[%d] = %x\n",i,writeData_va[i]);

    //Driver does not allow (single write length > 255)
    while(len > 255)
    {
        write_length = 255;
        ret = i2c_master_send(new_client, writeData_pa+write_count, write_length+1);
        //MATV_LOGD("[MATV_R]I2C Send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
            return 0;
        }
        write_count+=write_length;
        len -= write_length;
        if (bAutoInc){
            writeData_va[write_count] = cmd + write_count;
        }
        else{
            writeData_va[write_count] = cmd;
        }
    }
    if (len > 0){
        ret = i2c_master_send(new_client, writeData_pa+write_count, len+1);
        //MATV_LOGD("[MATV]I2C Send Size = %d\n",ret);
        if (ret < 0) {
            MATV_LOGE("[Error]MATV reads data error!! \n");
            return 0;
        }
    }

    return 1;
}

#endif

void matv_driver_init(void)
{
    /* Get MATV6326 ECO version */
    MATV_LOGD("******** mt5912 matv init\n");

    //PWR Enable
    mt_set_gpio_mode(GPIO_MATV_PWR_ENABLE,GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_MATV_PWR_ENABLE, GPIO_DIR_OUT);
    mt_set_gpio_pull_enable(GPIO_MATV_PWR_ENABLE,true);
    mt_set_gpio_out(GPIO_MATV_PWR_ENABLE, GPIO_DATA_OUT_DEFAULT);

    //n_Reset
    mt_set_gpio_mode(GPIO_MATV_N_RST,GPIO_MODE_00);
    mt_set_gpio_dir(GPIO_MATV_N_RST, GPIO_DIR_OUT);
    mt_set_gpio_pull_enable(GPIO_MATV_N_RST,true);
    mt_set_gpio_out(GPIO_MATV_N_RST, GPIO_DATA_OUT_DEFAULT);

    //init spin lock
    ///spin_lock_init(&g_mATVLock);
    matv_lock_init;
    

}

static int matv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int *user_data_addr;
	int ret = 0;
	U8 *pReadData = 0;
  	U8 *pWriteData = 0;
    U8 *ptr;
    U8 reg8, bAutoInc;
    U16 len;

    switch(cmd)
    {
        case TEST_MATV_PRINT :
			MATV_LOGD("**** mt5192 matv ioctl : test\n");
            break;
		
		case MATV_READ:			

            user_data_addr = (int *)arg;
            ret = copy_from_user(matv_in_data, user_data_addr, 4);
            ptr = (U8*)matv_in_data;
            reg8 = ptr[0];
            bAutoInc = ptr[1];
            len = ptr[2];
            len+= ((U16)ptr[3])<<8;
            //MATV_LOGD("**** mt5192 matv ioctl : read length = %d\n",len);
#ifdef _MATV_HIGH_SPEED_DMA_
            pReadData = gpDMABuf_va;
            pa_addr   = gpDMABuf_pa;
            if(!pReadData){
                MATV_LOGE("[Error] dma_alloc_coherent failed!\n");
                break;
            }
            mt5192_dma_read_m_byte(reg8, pReadData, pa_addr, len, bAutoInc);            
            ret = copy_to_user(user_data_addr, pReadData, len);
#else
            pReadData = (U8 *)kmalloc(len,GFP_ATOMIC);
            if(!pReadData){
                MATV_LOGE("[Error] kmalloc failed!\n");
                break;
            }
            mt5192_read_m_byte(reg8, pReadData, len, bAutoInc);
            ret = copy_to_user(user_data_addr, pReadData, len);
            if(pReadData)
                kfree(pReadData);
#endif //#ifdef _MATV_HIGH_SPEED_DMA_
            break;	
			
		case MATV_WRITE:			

            user_data_addr = (int *)arg;
            ret = copy_from_user(matv_in_data, user_data_addr, 4);
            ptr = (U8*)matv_in_data;
            reg8 = ptr[0];
            bAutoInc = ptr[1];
            len = ptr[2];
            len+= ((U16)ptr[3])<<8;
            //MATV_LOGD("**** mt5192 matv ioctl : write length = %d\n",len);            
#ifdef _MATV_HIGH_SPEED_DMA_
            pWriteData = gpDMABuf_va;
            pa_addr    = gpDMABuf_pa;
            if(!pWriteData){
                MATV_LOGE("[Error] dma_alloc_coherent failed!\n");
                break;
            }
            ret = copy_from_user(pWriteData+1, ((void*)user_data_addr)+4, len);
            //printk("\n[MATV]Write data = %d\n",*(pWriteData+1));
            mt5192_dma_write_m_byte(reg8, pWriteData, pa_addr, len, bAutoInc);            
#else
            pWriteData = (U8 *)kmalloc(len,GFP_ATOMIC);
            if(!pWriteData){
                MATV_LOGE("[Error] kmalloc failed!\n");
                break;
            }
            ret = copy_from_user(pWriteData, ((void*)user_data_addr)+4, len);
            //printk("\n[MATV]Write data = %d\n",*pWriteData);
            mt5192_write_m_byte(reg8, pWriteData, len, bAutoInc);
            //ret = copy_to_user(user_data_addr, pReadData, len);
            if(pWriteData)
                kfree(pWriteData);
#endif //#ifdef _MATV_HIGH_SPEED_DMA_
            break;
        case MATV_SET_PWR:
			user_data_addr = (int *)arg;
			ret = copy_from_user(matv_in_data, user_data_addr, sizeof(int));
			//MATV_LOGD("**** mt5192 matv ioctl : set pwr = %d\n",user_data_addr[0]);
            if(matv_in_data[0]!=0)
                mt_set_gpio_out(GPIO_MATV_PWR_ENABLE, GPIO_OUT_ONE);
            else
                mt_set_gpio_out(GPIO_MATV_PWR_ENABLE, GPIO_OUT_ZERO);
            break;
        case MATV_SET_RST:
			user_data_addr = (int *)arg;
			ret = copy_from_user(matv_in_data, user_data_addr, sizeof(int));
            //MATV_LOGD("**** mt5192 matv ioctl : set rst = %d\n",user_data_addr[0]);			
            if(matv_in_data[0]!=0){
                mt_set_gpio_out(GPIO_MATV_N_RST, GPIO_OUT_ONE);
            }
            else
                mt_set_gpio_out(GPIO_MATV_N_RST, GPIO_OUT_ZERO);            
            break;
        case MATV_SET_STRAP:
			user_data_addr = (int *)arg;
			ret = copy_from_user(matv_in_data, user_data_addr, sizeof(int));
            if(matv_in_data[0]==0){
               //Enable I2D Data pin and pull low
               mt_set_gpio_mode(GPIO_MATV_I2S_DATA,GPIO_MODE_00);
               mt_set_gpio_dir(GPIO_MATV_I2S_DATA, GPIO_DIR_OUT);
               mt_set_gpio_pull_enable(GPIO_MATV_I2S_DATA,true);
               mt_set_gpio_out(GPIO_MATV_I2S_DATA, GPIO_OUT_ZERO);
               printk("force I2s data pin low \n");
               //~
            }
            else if (matv_in_data[0]==1) {
               //Disable I2D Data pin
               mt_set_gpio_mode(GPIO_MATV_I2S_DATA,GPIO_MATV_I2S_DATA_M);
               mt_set_gpio_pull_enable(GPIO_MATV_I2S_DATA,false);
               mt_set_gpio_out(GPIO_MATV_I2S_DATA, GPIO_OUT_ZERO); 
               printk("put I2S data pin back \n");   
               //~  
            }
            break;            
        case MATV_SLEEP:
            {
                long timeout_jiff;
                static wait_queue_head_t matvWaitQueue;
                struct timeval t1,t2;
                int time_diff = 0;                
                int timeOut = 0;
                
                init_waitqueue_head(&matvWaitQueue);
                user_data_addr = (int *)arg;
    			ret = copy_from_user(matv_in_data, user_data_addr, sizeof(int));
                timeout_jiff = (matv_in_data[0]+2) * HZ / 1000; // wait 80 ms
                do_gettimeofday(&t1);
                timeOut = wait_event_interruptible_timeout(matvWaitQueue, NULL, timeout_jiff);
                if(0 != timeOut)
                    MATV_LOGE("[MATV] Fail to sleep enough time %d\n", timeOut);
                do_gettimeofday(&t2);
                time_diff = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);
                if (time_diff < (matv_in_data[0]-2)*1000){
                    //MATV_LOGE("[MATV]TimeDiff=%d\n",time_diff);
                    udelay(matv_in_data[0]*1000 - time_diff);
                }
            }
        
            break;
        case MATV_SET_TP_MODE:
            {
                user_data_addr = (int *)arg;
                ret = copy_from_user(matv_in_data, user_data_addr, sizeof(int));
                MATV_LOGD("[MATV]MATV_SET_TP_MODE = %d\n",matv_in_data[0]);
                if(matv_in_data[0] == 0)
                {
                    tpd_switch_single_mode();
                }
                else if(matv_in_data[0] == 1)
                {
                    tpd_switch_multiple_mode(); 
                }
                else if(matv_in_data[0] == 2)
                {
                    tpd_switch_sleep_mode();
                }
                else if(matv_in_data[0] == 3)
                {
                    tpd_switch_normal_mode();
                }
                else
                {
                    MATV_LOGE("[MATV] TP's mode value(%d) is wrong!\n",matv_in_data[0]);
                }
            }
        
            break;
        default:
            break;
    }

    return 0;
}

static int matv_open(struct inode *inode, struct file *file)
{ 
    MATV_LOGD("******** mt5912 matv open %d, %d\n", g_mATVCnt, g_mI2CErrCnt);

    matv_lock;
    if((++g_mATVCnt) >1)
    {
        matv_unlock;
        return 0;
    }
    if(0!=cust_matv_power_on())
    {
        MATV_LOGE("[MATV] Fail to power on analog gain\n");
        g_mATVCnt--;
        matv_unlock;
        
        return -EIO;
    }

#ifdef _MATV_HIGH_SPEED_DMA_    
    gpDMABuf_va = (u8 *)dma_alloc_coherent(NULL, 4096, &gpDMABuf_pa, GFP_KERNEL);
    if(!gpDMABuf_va){
        MATV_LOGE("[MATV][Error] Allocate DMA I2C Buffer failed!\n");
    }
#endif
	g_mI2CErrCnt = 0;

    matv_unlock;


   return 0;
}

static int matv_release(struct inode *inode, struct file *file)
{
    MATV_LOGD("******** mt5912 matv release %d I2C Err: %d\n", g_mATVCnt, g_mI2CErrCnt);
    matv_lock;

	if((--g_mATVCnt) > 0)
    {
        matv_unlock;
        return 0;
    }
	
    mt_set_gpio_out(GPIO_MATV_PWR_ENABLE, GPIO_OUT_ZERO);
    mt_set_gpio_out(GPIO_MATV_N_RST, GPIO_OUT_ZERO);
    //Disable I2D Data pin
#ifdef _MATV_HIGH_SPEED_DMA_    
    if(gpDMABuf_va){
        dma_free_coherent(NULL, 4096, gpDMABuf_va, gpDMABuf_pa);
        gpDMABuf_va = NULL;
        gpDMABuf_pa = NULL;
    }
#endif

    if(0!=cust_matv_power_off())
    {
        MATV_LOGE("[MATV] Fail to power off analog gain\n");     
        matv_unlock;
        return -EIO;
    }
    //Switch TP back to normal mode
    tpd_switch_normal_mode();    

	if(g_mATVCnt < 0)
		g_mATVCnt = 0;

	g_mI2CErrCnt = 0;
    matv_unlock;
    return 0;
}

static struct file_operations matv_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl		= matv_ioctl,
	.open		= matv_open,
	.release	= matv_release,	
};

#ifdef _NEW_I2C_DRV_
static int matv_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info) {         
    strcpy(info->type, MATV_I2C_DEVNAME);                                                         
    return 0;                                                                                       
}                                                                                                  
                                                                                                   
static int matv_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id) {             
    struct class_device *class_dev = NULL;
    ///int err=0;
	int ret=0;

    MATV_LOGD("[mATV]mt5192_i2c_probe !!\n ");

	/* Integrate with META TOOL : START */
	ret = alloc_chrdev_region(&matv_devno, 0, 1, MATV_DEVNAME);
	if (ret) 
		MATV_LOGE("Error: Can't Get Major number for matv \n");
	matv_cdev = cdev_alloc();
    matv_cdev->owner = THIS_MODULE;
    matv_cdev->ops = &matv_fops;
    ret = cdev_add(matv_cdev, matv_devno, 1);
	if(ret)
	    MATV_LOGE("matv Error: cdev_add\n");
	matv_major = MAJOR(matv_devno);
	matv_class = class_create(THIS_MODULE, MATV_DEVNAME);
    class_dev = (struct class_device *)device_create(matv_class, 
													NULL, 
													matv_devno, 
													NULL, 
													MATV_DEVNAME);

    new_client = client;
#if 0
    {
        unsigned long khz = 400;
        unsigned long tmp, sclk, hclk = 13000;
        unsigned short sample_cnt_div, step_cnt_div;
		unsigned long diff, min_diff = 13000/*I2C_CLK_RATE*/;
		unsigned short sample_div = 8/*MAX_SAMPLE_CNT_DIV*/;
		unsigned short step_div = 64/*max_step_cnt_div*/;
		for (sample_cnt_div = 1; sample_cnt_div <= 8/*MAX_SAMPLE_CNT_DIV*/; sample_cnt_div++) {
			for (step_cnt_div = 1; step_cnt_div <= 64/*max_step_cnt_div*/; step_cnt_div++) {
				sclk = (hclk >> 1) / (sample_cnt_div * step_cnt_div);
				if (sclk > khz) 
					continue;
				diff = khz - sclk;
				if (diff < min_diff) {
					min_diff = diff;
					sample_div = sample_cnt_div;
					step_div   = step_cnt_div;
				}											
			}
		}
		sample_cnt_div = sample_div;
		step_cnt_div   = step_div;

        step_cnt_div--;
	    sample_cnt_div--;

        new_client->timing = (sample_cnt_div & 0x7) << 8 | (step_cnt_div & 0x1f) << 0;
        MATV_LOGD("new client's timing = %x\n",new_client->timing);
	}
#endif

#if defined(_MATV_HIGH_SPEED_)
    /*
        new_client->addr = new_client->addr | I2C_A_CHANGE_TIMING;
        new_client->timing = 0x10;
    */
    new_client->addr = new_client->addr & I2C_MASK_FLAG;        
    new_client->timing = 400;    
#elif defined(_MATV_HIGH_SPEED_DMA_)
    new_client->addr = new_client->addr & I2C_MASK_FLAG | I2C_DMA_FLAG;
    new_client->timing = 400;
#endif
        //MATV_LOGD("new client's address = %x\n",new_client->addr);
        //MATV_LOGD("new client's timing = %x\n",new_client->timing);

    matv_driver_init();

    return 0;                                                                                       
} 

static int matv_i2c_remove(struct i2c_client *client)
{
    return 0;
}

struct i2c_driver matv_i2c_driver = {                       
    .probe = matv_i2c_probe,                                   
    .remove = matv_i2c_remove,                           
    .detect = matv_i2c_detect,                           
    .driver.name = MATV_I2C_DEVNAME,                 
    .id_table = my_i2c_id,                             
    .address_data = &addr_data,                        
};  

#else
/* This function is called by i2c_detect */
int mt5192_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct class_device *class_dev = NULL;
    int err=0;
	int ret=0;

    //MATV_LOGD("[mATV]mt5192_detect !!\n ");

	/* Integrate with META TOOL : START */
	ret = alloc_chrdev_region(&matv_devno, 0, 1, MATV_DEVNAME);
	if (ret) 
		MATV_LOGE("Error: Can't Get Major number for matv \n");
	matv_cdev = cdev_alloc();
    matv_cdev->owner = THIS_MODULE;
    matv_cdev->ops = &matv_fops;
    ret = cdev_add(matv_cdev, matv_devno, 1);
	if(ret)
	    MATV_LOGE("matv Error: cdev_add\n");
	matv_major = MAJOR(matv_devno);
	matv_class = class_create(THIS_MODULE, MATV_DEVNAME);
    class_dev = (struct class_device *)device_create(matv_class, 
													NULL, 
													matv_devno, 
													NULL, 
													MATV_DEVNAME);
	//MATV_LOGD("MATV META Prepare : Done !!\n ");
	/* Integrate with META TOOL : END */

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        goto exit;

    if (!(new_client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
        err = -ENOMEM;
        goto exit;
    }	
    memset(new_client, 0, sizeof(struct i2c_client));

    new_client->addr = address;
    new_client->adapter = adapter;
    new_client->driver = &mt5192_driver;
    new_client->flags = 0;
    strncpy(new_client->name, "mt5192", I2C_NAME_SIZE);

    if ((err = i2c_attach_client(new_client)))
        goto exit_kfree;

    matv_driver_init();

    return 0;

exit_kfree:
    kfree(new_client);
exit:
    return err;
}

static int mt5192_attach_adapter(struct i2c_adapter *adapter)
{
    //MATV_LOGD("[MATV] mt5192_attach_adapter (id=%d)******\n",adapter->id);
    if (adapter->id == MATV_I2C_CHANNEL)
    	return i2c_probe(adapter, &addr_data, mt5192_detect);
    return -1;
}

static int mt5192_detach_client(struct i2c_client *client)
{
	int err;

    device_destroy(matv_class, matv_devno);
    class_destroy(matv_class);
   
    cdev_del(matv_cdev);
    unregister_chrdev_region(matv_devno, 1);

	err = i2c_detach_client(client);
	if (err) {
		dev_err(&client->dev, "Client deregistration failed, client not detached.\n");
		return err;
	}

	kfree(i2c_get_clientdata(client));
	
	return 0;
}
#endif

static int matv_probe(struct platform_device *dev)
{ 
    MATV_LOGD("[MATV] probe done\n");
    return 0;
}

static int matv_remove(struct platform_device *dev)
{
    MATV_LOGD("[MATV] remove\n");
    return 0;
}

static void matv_shutdown(struct platform_device *dev)
{
    MATV_LOGD("[MATV] shutdown\n");
}

static int matv_suspend(struct platform_device *dev, pm_message_t state)
{    
    MATV_LOGD("[MATV] suspend\n");
    return 0;
}

static int matv_resume(struct platform_device *dev)
{   
    MATV_LOGD("[MATV] resume\n");
    return 0;
}

static struct platform_driver matv_driver = {
    .probe       = matv_probe,
    .remove      = matv_remove,
    .shutdown    = matv_shutdown,
    .suspend     = matv_suspend,
    .resume      = matv_resume,
    .driver      = {
    .name        = MATV_DEVNAME,
    },
};

static struct platform_device matv_device = {
    .name     = MATV_DEVNAME,
    .id       = 0,
};

static int __init mt5192_init(void)
{
    int ret;
    
    MATV_LOGD("[MATV] mt5192_init ******\n");
#ifdef _NEW_I2C_DRV_  
    if (i2c_add_driver(&matv_i2c_driver)){
        MATV_LOGE("[MATV][ERROR] fail to add device into i2c\n");
        ret = -ENODEV;
        return ret;
    }
#else
    if (i2c_add_driver(&mt5192_driver)){
        MATV_LOGE("[MATV][ERROR] fail to add device into i2c\n");
        ret = -ENODEV;
        return ret;
    }
#endif
    if (platform_device_register(&matv_device)){
        MATV_LOGE("[MATV][ERROR] fail to register device\n");
        ret = -ENODEV;
        return ret;
    }
    
    if (platform_driver_register(&matv_driver)){
        MATV_LOGE("[MATV][ERROR] fail to register driver\n");
        platform_device_unregister(&matv_device);
        ret = -ENODEV;
        return ret;
    }

    //MATV_LOGD("[MATV] mt5192_init done******\n");

    return 0;
}

static void __exit mt5192_exit(void)
{
    MATV_LOGD("[MATV] mt5192_exit ******\n");

    platform_driver_unregister(&matv_driver);
    platform_device_unregister(&matv_device);    
	i2c_del_driver(&matv_i2c_driver);
}
module_init(mt5192_init);
module_exit(mt5192_exit);
   
MODULE_LICENSE("GPL");
//MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("MediaTek MATV mt5192 Driver");
MODULE_AUTHOR("Charlie Lu<charlie.lu@mediatek.com>");

