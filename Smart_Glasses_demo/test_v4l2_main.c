#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main(void) 
{
    int fd = open("/dev/video1",O_RDWR);
    if (fd < 0)
    {
        perror("打开设备失败");
        return -1;
    }

    //获取摄像头支持格式 ioctl(文件描述符,命令，与命令对应的结构体)
    struct v4l2_fmtdesc vfmt;
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//摄像头采集

    int i = 0;
    while(1)         //使用while循环从0开始测试index索引值
    {
        vfmt.index = i;
        i++;
        int ret = ioctl(fd,VIDIOC_ENUM_FMT,&vfmt);
        if (ret < 0)
        {
            perror("获取失败");
            break;
        }
        printf("index=%d\n",vfmt.index);
        printf("flags=%d\n",vfmt.flags);
        printf("discription=%s\n",vfmt.description);
        unsigned char *p = (unsigned char *)&vfmt.pixelformat;
        printf("pixelformat=%c%c%c%c\n",p[0],p[1],p[2],p[3]);
        printf("reserved=%d\n",vfmt.reserved[0]);
    }
    close(fd);
    return 0;
}
