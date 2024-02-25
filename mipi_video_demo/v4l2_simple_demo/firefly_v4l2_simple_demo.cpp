/*
2021-05-18 11:02:09

This program demo demonstrates how to call MIPI camera in C and C++, and use OpenCV for image display.


firefly
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h> /* getopt_long() */
#include <fcntl.h> /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <dlfcn.h>


#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <linux/videodev2.h>

using namespace cv;

//通过定义这个结构体，您可以方便地组织和管理与缓冲区相关的信息。start 和 length 成员可以用于访问和操作缓冲区的数据，
//而 v4l2_buf 成员可以用于存储 V4L2 相关的数据，如缓冲区的索引、状态等。
struct buffer {
        void *start;
        size_t length;
        struct v4l2_buffer v4l2_buf;
};

#define BUFFER_COUNT 4
#define FMT_NUM_PLANES 1
#define CLEAR(x) memset(&(x), 0, sizeof(x))

#define DBG(...) do { if(!silent) printf(__VA_ARGS__); } while(0)
#define ERR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)


static int fd = -1;
FILE *fp=NULL;
static unsigned int n_buffers;
struct buffer *buffers;
static int silent=0;

static char dev_name[255]="/dev/video0";
static int width = 640;
static int width = 640;
static int height = 480;
static enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
static int iformat = V4L2_PIX_FMT_NV12;


static void errno_exit(const char *s)
{
	ERR("%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}


// 下述代码中的 xioctl 函数使用一个循环来重复调用 ioctl，直到调用成功或者发生的错误不再是 EINTR 为止。
// 这样做的目的是确保在系统调用被中断时能够正确地重新执行 ioctl 调用，以完成所需的操作。
static int xioctl(int fh, int request, void *arg)
{
	int r;
	do {
			r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);
	return r;
}

// O_RDONLY: 只读打开
// O_WRONLY: 只写打开
// O_RDWR: 读写打开
// 上述三个常量，必须指定一个且只能指定一个
// O_CREAT:若文件不存在，则创建
// O_APPEND: 追加写
// O_TRUNC: 若文件存在，并且以只写/只读打开，则将清空文件内容
//参数0表示默认权限
static void open_device(void)
{
    fd = open(dev_name, O_RDWR /* required */ /*| O_NONBLOCK*/, 0);//打开dev_name这个设备

    if (-1 == fd) {
        ERR("Cannot open '%s': %d, %s\n",
                    dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

// 这段代码使用了 OpenCV 库的 namedWindow 函数，它用于创建一个图像显示窗口。
// 函数的参数是一个字符串，用于指定窗口的名称，这里指定为 "video"。
static void init_display_buf(int buffer_size, int width, int height)
{
	cv::namedWindow("video");
}

//函数 init_mmap 的作用是初始化内存映射方式的视频缓冲区
static void init_mmap(void)
{
	struct v4l2_requestbuffers req;//用于在视频（V4L2）中请求缓冲区。

	CLEAR(req);//通常，这样的宏定义用于将结构体变量的内存清零，以确保结构体的所有字段都被初始化为默认值或空值。
	           //如果 CLEAR(req) 是一个宏定义，那么它可能类似于以下示例,#define CLEAR(x) memset(&(x), 0, sizeof(x))
	req.count = BUFFER_COUNT;
	req.type = buf_type;//这段代码定义了一个静态的枚举类型变量 buf_type，并将其初始化为 V4L2_BUF_TYPE_VIDEO_CAPTURE。
	req.memory = V4L2_MEMORY_MMAP;//V4L2_MEMORY_MMAP是视频（V4L2）中的一个枚举值，表示使用内存映射（Memory Mapping）方式进行缓冲区的存储。
                                  //通过将 req.memory 设置为 V4L2_MEMORY_MMAP，您告诉 V4L2 驱动程序在请求的缓冲区中使用内存映射方式进行数据的读写。
	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
				ERR("%s does not support "
								"memory mapping\n", dev_name);
				exit(EXIT_FAILURE);
		} else {
				errno_exit("VIDIOC_REQBUFS");
		}
	}
    //确保缓冲区足够
	if (req.count < 2) {
		ERR("Insufficient buffer memory on %s\n",
						dev_name);
		exit(EXIT_FAILURE);
	}


	//calloc 函数是 C 标准库中的一个内存分配函数，它会分配一块指定大小的内存，并将其初始化为零。
	//在这里，calloc 函数被用来为 buffers 分配内存。
	buffers = (struct buffer*)calloc(req.count, sizeof(*buffers));
	//请注意，在使用完 buffers 后，您需要负责释放分配的内存，以避免内存泄漏。可以使用 free 函数释放 buffers 所指向的内存，如下所示：
	//free(buffers);
	if (!buffers) {
		ERR("Out of memory\n");
		exit(EXIT_FAILURE);
	}

	//用于对每个缓冲区进行初始化和内存映射。
	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;
		struct v4l2_plane planes[FMT_NUM_PLANES];
		CLEAR(buf);
		CLEAR(planes);

		buf.type = buf_type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
			buf.m.planes = planes;
			buf.length = FMT_NUM_PLANES;
		}

		if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
			buffers[n_buffers].length = buf.m.planes[0].length;
			buffers[n_buffers].start =
			mmap(NULL /* start anywhere */,
					buf.m.planes[0].length,
					PROT_READ | PROT_WRITE /* required */,
					MAP_SHARED /* recommended */,
					fd, buf.m.planes[0].m.mem_offset);
		} else {
			buffers[n_buffers].length = buf.length;
			buffers[n_buffers].start =
			mmap(NULL /* start anywhere */,
					buf.length,
					PROT_READ | PROT_WRITE /* required */,
					MAP_SHARED /* recommended */,
					fd, buf.m.offset);
		}

		if (MAP_FAILED == buffers[n_buffers].start)
				errno_exit("mmap");
	}
}
//init_device 函数的作用是初始化视频设备的相关设置
//作用是通过与 V4L2 驱动程序的交互，
//初始化视频设备的能力、格式和缓冲区，为后续的视频采集和显示操作做准备。
static void init_device(void)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
            if (EINVAL == errno) {
                    ERR("%s is no V4L2 device\n",
                                dev_name);
                    exit(EXIT_FAILURE);
            } else {
                    errno_exit("VIDIOC_QUERYCAP");
            }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        ERR("%s is not a video capture device, capabilities: %x\n",
                        dev_name, cap.capabilities);
            exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
            ERR("%s does not support streaming i/o\n",
                dev_name);
            exit(EXIT_FAILURE);
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    CLEAR(fmt);
    fmt.type = buf_type;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = iformat;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    init_mmap();

    init_display_buf(fmt.fmt.pix.sizeimage, width, height);
}


static void start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;
	//通过一个循环遍历视频缓冲区数组，对每个缓冲区进行设置和入队操作
	for (i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf;

			CLEAR(buf);
			buf.type = buf_type;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;

			if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
				struct v4l2_plane planes[FMT_NUM_PLANES];

				buf.m.planes = planes;
				buf.length = FMT_NUM_PLANES;
			}
			if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
					errno_exit("VIDIOC_QBUF");
	}
	type = buf_type;
	if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
}


static void process_buffer(struct buffer* buff, int size)
{
	if (fp) {
		fwrite(buff->start, size, 1, fp);
		fflush(fp);
	}

	cv::Mat yuvmat(cv::Size(width, height*3/2), CV_8UC1, buff->start);
	cv::Mat rgbmat(cv::Size(width, height), CV_8UC3);
	cv::cvtColor(yuvmat, rgbmat, cv::COLOR_YUV2BGR_NV12);
	cv::imshow("video", rgbmat);
	cv::waitKey(1);
}

//用于从视频设备中读取一帧数据。
static int read_frame()
{
	struct v4l2_buffer buf;
	int i, bytesused;
	//CLEAR(buf) 是一个宏定义，用于将 struct v4l2_buffer 类型的变量 buf 清零。
	CLEAR(buf);

	buf.type = buf_type;
	buf.memory = V4L2_MEMORY_MMAP;

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type) {
			struct v4l2_plane planes[FMT_NUM_PLANES];
			buf.m.planes = planes;
			buf.length = FMT_NUM_PLANES;
	}
//调用 xioctl 函数，使用 VIDIOC_DQBUF 命令从视频设备中取出一帧数据，并将数据填充到 buf 中。
//这个操作将视频缓冲区中的数据传输到用户空间

	if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
			errno_exit("VIDIOC_DQBUF");

	i = buf.index;

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type)
			bytesused = buf.m.planes[0].bytesused;
	else
			bytesused = buf.bytesused;
			process_buffer(&(buffers[i]), bytesused);
	DBG("bytesused %d\n", bytesused);

	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
			errno_exit("VIDIOC_QBUF");

	return 1;
}
//获取当前时间戳（以毫秒为单位）
static unsigned long get_time(void)
{
	struct timeval ts;
	gettimeofday(&ts, NULL);
	return (ts.tv_sec * 1000 + ts.tv_usec / 1000);
}


static void mainloop(void)
{
	unsigned int count = 1;
	unsigned long read_start_time, read_end_time;

	while (1) {

		DBG("No.%d\n", count);        //Display the current image frame number

		read_start_time = get_time();
		read_frame();
		read_end_time = get_time();

		DBG("take time %lu ms\n",read_end_time - read_start_time);
	}
	DBG("\nREAD AND SAVE DONE!\n");
}


int main(int argc, char *argv[])
{
    open_device();
    init_device();
    start_capturing();
    mainloop();

    return 0;
}
