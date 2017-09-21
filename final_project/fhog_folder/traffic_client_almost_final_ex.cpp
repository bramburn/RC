#include <dlib/image_processing.h>
#include <dlib/image_transforms.h>
#include <dlib/gui_widgets.h>
#include <dlib/svm_threaded.h>
#include <dlib/gui_widgets.h>
#include <dlib/data_io.h>
#include <dlib/opencv.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/mat.hpp>  
#include <opencv2/imgcodecs.hpp>  

#include <iostream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <sys/socket.h> 
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>

using namespace std;
using namespace cv;

namespace HSV_CONST
{
	enum
	{
		RED_HL =170, RED_SL =50, RED_VL =142,\
		RED_HH =179, RED_SH =255, RED_VH =255,\
		GREEN_HL =38, GREEN_SL =50, GREEN_VL =120,\
		GREEN_HH =80, GREEN_SH =255, GREEN_VH =255
	};
}

void* img_recv(void*);
void* img_handler(void*);
void* snd_handler_ts(void*);
void create_msg_box(vector<dlib::rectangle> &, dlib::point* (&), Point* (&), int);
void create_color_box(Mat* (&), Point* (&), int);
int tlight_msg_handler(Mat &);
int tsign_msg_handler(Mat &, int, dlib::rectangle &, char*);
int hsv_handler(Mat &);
int red_detect(Mat &);
int green_detect(Mat &);
float dist_detect(dlib::rectangle &);

// 전역변수 class private로 바꾸기
// 함수 깔끔하게 정리


Mat img;
sem_t recv_sync;
sem_t show_sync;
pthread_mutex_t I_Mutex;
int red_sign_on, child_sign_on, buf;

int main(int argc, char** argv)
{

	int connSock;
	struct sockaddr_in server_addr;
	char*serverAddr;
	int serverPort; 
	int thr_id[3], status;
	pthread_t pid[3];
	int len;
	
	img = Mat::zeros(240, 320, CV_8UC3);
	
	if(argc < 3)
	{
		perror("Usage: IP_address Port_num");
		return -1;
	}

	serverAddr = argv[1];
	serverPort = atoi(argv[2]);

	if((connSock=socket(PF_INET, SOCK_STREAM, 0)) < 0) 
	{
		perror("Traffic client can't open socket");
		return -1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(serverAddr);
	server_addr.sin_port = htons(serverPort);

	if(connect(connSock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
	{
		perror("Traffic client can't connect");
		return -1;
	}

	printf("Traffic Client connect to Traffic server \n");

	if(thr_id[0]=pthread_create(&pid[0], NULL, img_recv, NULL) < 0)
	{
		perror("pthread1 failed");
		return -1;
	}
	if(thr_id[1]=pthread_create(&pid[1], NULL, img_handler, (void*)&connSock) < 0)
	{
		perror("pthread2 failed");
		return -1;
	}
	if(thr_id[2]=pthread_create(&pid[2], NULL, snd_handler_ts, NULL) < 0)
	{
		perror("pthread2 failed");
		return -1;
	}

	pthread_join(pid[0], (void**)&status);
	pthread_join(pid[1], (void**)&status);
	pthread_join(pid[2], (void**)&status);

	close(connSock);

	return 0;
}

void* img_recv(void*arg)
{
	VideoCapture vcap; 
 
	string videoStreamAddress = "rtsp://192.168.1.105:8554/test";	
	
	vcap.open(videoStreamAddress);
	vcap.set(CV_CAP_PROP_CONVERT_RGB,1);

	while(1) 
	{ 
		vcap.read(img);
		sem_post(&recv_sync);	
	} 		
}

void* img_handler(void*arg)
{
	int connSock = *(int*)arg;
	int detect_color = 0;
	
	typedef dlib::scan_fhog_pyramid<dlib::pyramid_down<6>> image_scanner_type; 
	
	image_scanner_type scanner_ts;
	image_scanner_type scanner_tl;

	dlib::object_detector<image_scanner_type> detector_tsign;
	dlib::object_detector<image_scanner_type> detector_tlight;

	dlib::matrix <dlib::bgr_pixel>cimg;

	Mat*Color;

	dlib::point*array_dpt_ts;
	Point*array_cpt_ts;

	dlib::point*array_dpt_tl;
	Point*array_cpt_tl;

	vector<dlib::rectangle> dets_tsign;
	vector<dlib::rectangle> dets_tlight;

	dlib::deserialize("tsign_detector.svm") >> detector_tsign;
	dlib::deserialize("tlight3_detector.svm") >> detector_tlight;

	scanner_ts.set_detection_window_size(80, 80); 
	scanner_ts.set_max_pyramid_levels(1); 
	scanner_tl.set_detection_window_size(80, 170); 
	scanner_tl.set_max_pyramid_levels(1);	

	while(1)
	{
		sem_wait(&recv_sync);

		dlib::assign_image(cimg,dlib::cv_image<dlib::bgr_pixel>(img));
	
		dlib::pyramid_up(cimg);	

		dets_tsign = detector_tsign(cimg);
		dets_tlight = detector_tlight(cimg);

		if(dets_tsign.size())
		{
			static int i = 0;

			array_dpt_ts=new dlib::point[dets_tsign.size()<<1];
			array_cpt_ts=new Point[dets_tsign.size()<<1];

			create_msg_box(dets_tsign, array_dpt_ts, array_cpt_ts, dets_tsign.size());
			cout<<"deteced child == "<<dets_tsign.size()<<' '<<"slow!!"<<endl;

			child_sign_on = 1;
		
			for(int i=0;i<dets_tsign.size();i++)
			{
				rectangle(img, array_cpt_ts[i*2], array_cpt_ts[i*2+1], cv::Scalar(0, 255, 0),3);
			}

			delete []array_dpt_ts;
			delete []array_cpt_ts;
		}
		else
		{
			cout<<"detected child == "<<dets_tsign.size()<<endl;
			child_sign_on = 0;
		}

		if(dets_tlight.size())
		{
			static int i = 0;

			Color = new Mat[dets_tlight.size()];
			array_dpt_tl = new dlib::point[dets_tlight.size()<<1];
			array_cpt_tl = new Point[dets_tlight.size()<<1];

			create_msg_box(dets_tlight, array_dpt_tl, array_cpt_tl, dets_tlight.size());
	
			cout<<"0x  0y   1x  1y"<<' '<<array_cpt_tl[0].x<<' '<<array_cpt_tl[0].y<<' '<<array_cpt_tl[1].x<<' '<<array_cpt_tl[1].y<<endl;

			create_color_box(Color, array_cpt_tl, dets_tlight.size());
		
		
			for(int i=0;i<dets_tlight.size();i++)
			{
				detect_color = tlight_msg_handler(Color[i]);
			
				if(detect_color>>1)
				{
					cout<<"detected traffic light GREEN "<<i++<<endl;
					red_sign_on = 0;
					rectangle(img, array_cpt_tl[0], array_cpt_tl[1], cv::Scalar(0, 255, 0),3);
				}
				else
				{
					if(detect_color)
					{
						cout<<"detected traffic light RED == STOP!!"<<i++<<endl;
						red_sign_on = 2;
						rectangle(img, array_cpt_tl[0], array_cpt_tl[1], cv::Scalar(0, 255, 0),3);
					}

					red_sign_on = 0;
				}
			}

			delete []Color;
			delete []array_dpt_tl;
			delete []array_cpt_tl;
		}
		else
			red_sign_on = 0;


		buf= red_sign_on | child_sign_on; // 0 fast, 1 slow, 2 stop, 3 slow and stop
		
		if(send(connSock, &buf, sizeof(buf), 0) < 0) 
		{
			perror("send to traffic server failed");
		}
			
		detect_color = 0;
      	
		imshow("Output Window", img); 
		waitKey(1);
	}
}

void create_msg_box(vector<dlib::rectangle> &dets, dlib::point*(&array_dpt), Point*(&array_cpt), int size)
{
	for(int i=0;i<size;i++)
	{
		array_dpt[i*2]=dets[i].tl_corner();
		array_dpt[i*2+1]=dets[i].br_corner();

		array_cpt[i*2].x=array_dpt[i*2].x()>>1;
		array_cpt[i*2].y=array_dpt[i*2].y()>>1;
		array_cpt[i*2+1].x=array_dpt[i*2+1].x()>>1;
		array_cpt[i*2+1].y=array_dpt[i*2+1].y()>>1;
	}
}

void create_color_box(Mat* (&Color), Point*(&array_cpt_tl), int size)
{
	for(int i=0;i<size;i++)
	{
		if(array_cpt_tl[i*2].x < 0)
		{
			if(abs(array_cpt_tl[i*2].x) < img.cols>>1)
				array_cpt_tl[i*2].x = 0;
		}

		if(array_cpt_tl[i*2+1].x > img.cols)
		{
			array_cpt_tl[i*2+1].x = img.cols;
		}

		if(array_cpt_tl[i*2].y < 0)
		{
			if(abs(array_cpt_tl[i*2].y) < img.rows>>1) 
				array_cpt_tl[i*2].y = 0;
		}

		if(array_cpt_tl[i*2+1].y > img.rows)
		{
			array_cpt_tl[i*2+1].y = img.rows;
		}

	}

	for(int i=0;i<size;i++)
	{
		Rect roi(array_cpt_tl[i*2].x, array_cpt_tl[i*2].y, array_cpt_tl[i*2+1].x - array_cpt_tl[i*2].x, array_cpt_tl[i*2+1].y - array_cpt_tl[i*2].y);
		Color[i]=img(roi);
	}
}	

void* snd_handler_ts(void*arg)
{
	while(1)
	{
		if(buf&1)
		{
			int ret = system("mpg123 child_sign_voice.mp3");
		}
	}
}

int tlight_msg_handler(Mat &img)
{
	int i = 0;
	int red_positive;
		
	red_positive = hsv_handler(img);

	if(red_positive>>1)
	{
		//cout<<endl;
		//cout<<"redsign == Stop"<<' '<<i<<endl;
		//cout<<endl;
		i++;
		return 2;
	}
	else
	{
		//cout<<endl;
		//cout<<"no red sign == Go"<<endl;
		if(red_positive)
			return 1;
	}

	return 0;
}

//int tsign_msg_handler(Mat &img, int dets, dlib::rectangle &r, char*distance_msg)
//{
//	
//	float distance;
//	int i = 0;
//	int red_positive;
//		
//	if(dets)
//	{
//		distance=dist_detect(r);
//		sprintf(distance_msg,"  DISTANCE : %.2fCM\n",distance);
//		
//		if(distance < 20)
//		{
//			child_sign_on = 1;
//			cout<<"Stop"<<' '<<i<<endl;
//			i++;
//		}
//	}
//
//	sprintf(distance_msg,"  detected sign : %d\n",dets);
//
//	return 0;
//}

float dist_detect(dlib::rectangle &r)
{
	//float v = (r.bottom() - r.top())/2+r.top();
	float v = (r.right() - r.left())<<2;
	float distance = 14/tan ((-3.1) + atan((v- 119.8) / 332.3));
	cout<< "distance = "<<distance<<"cm"<<endl;
	
	return distance;
}

int hsv_handler(Mat &img)
{
	Mat img_for_detect[2];
	int red_positive, green_positive;

	green_positive=green_detect(img);
	red_positive=red_detect(img);

	if(green_positive > 1)
		return 2;
	else if(red_positive > 1)
		return 1;
	else
		return 0;
}

int red_detect(Mat &img_for_detect)
{
	Mat img_binary, img_hsv;

	cvtColor(img_for_detect, img_hsv, COLOR_BGR2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::RED_HL,HSV_CONST::RED_SL,HSV_CONST::RED_VL),\
		Scalar(HSV_CONST::RED_HH,HSV_CONST::RED_SH,HSV_CONST::RED_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 8, CV_32S);  

	//cout<<"detected reds : "<<numOfLables-1<<endl;

	return numOfLables;
}

int green_detect(Mat &img_for_detect)
{
	Mat img_binary, img_hsv;

	cvtColor(img_for_detect, img_hsv, COLOR_BGR2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::GREEN_HL,HSV_CONST::GREEN_SL,HSV_CONST::GREEN_VL),\
		Scalar(HSV_CONST::GREEN_HH,HSV_CONST::GREEN_SH,HSV_CONST::GREEN_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 4, CV_32S);  

	//cout<<"detected greens : "<<numOfLables-1<<endl;

	return numOfLables;
}
