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

#include <semaphore.h>
#include <fcntl.h>
#include <sys/socket.h> 
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>


using namespace std;
using namespace cv;

namespace HSV_CONST
{
	enum
	{
		RED_HL =170, RED_SL =50, RED_VL =142,\
		RED_HH =179, RED_SH =255, RED_VH =255,\
		GREEN_HL =38, GREEN_SL =70, GREEN_VL =70,\
		GREEN_HH =80, GREEN_SH =255, GREEN_VH =255
	};
}

Mat img;
sem_t img_sync;

void*msg_send(void*);
void*img_recv(void*);
void*img_handler(void*);
int tlight_msg_handler(Mat &);
int tsign_msg_handler(Mat &, int, dlib::rectangle &, char*);
void create_msg_box(std::vector<dlib::rectangle> &, dlib::rectangle &);
int hsv_handler(Mat &);
int red_detect(Mat &);
int green_detect(Mat &);
float dist_detect_tl(dlib::rectangle &);
float dist_detect_ts(dlib::rectangle &);

int red_sign_on, child_sign_on, buf;

int main(int argc, char** argv)
{

	int connSock;
	struct sockaddr_in server_addr;
	char*serverAddr;
	int serverPort; 
	int thr_id, status;
	pthread_t pid[3];
	int len;

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

	thr_id=pthread_create(&pid[0], NULL,img_recv,(void*)&connSock);
	thr_id=pthread_create(&pid[1], NULL,img_handler,(void*)&connSock);
	thr_id=pthread_create(&pid[2], NULL,msg_send,(void*)&connSock);

	pthread_join(pid[0], (void**)&status);
	pthread_join(pid[1], (void**)&status);
	pthread_join(pid[2], (void**)&status);

	close(connSock);

	return 0;
}

void*img_recv(void*arg)
{
	int connSock=*(int*)arg;

	while(1)
	{
		img = Mat::zeros(240,320,CV_8UC3);	   
		int imgSize = img.total() * img.elemSize();		
		uchar *iptr = img.data;
		if(recv(connSock, iptr, imgSize, MSG_WAITALL) < 0) 
		{
			perror("receive from server failed");
		}
		
		imshow("Output Window", img); 
		waitKey(1);
		sem_post(&img_sync);
	}
}

void*img_handler(void*arg)
{
	Mat img_show;

	int connSock=*(int*)arg;
	int detect_color = 0;

	typedef dlib::scan_fhog_pyramid<dlib::pyramid_down<6> > image_scanner_type; 
	
	image_scanner_type scanner_tl;
	image_scanner_type scanner_ts;

	dlib::object_detector<image_scanner_type> detector_tlight;
	dlib::object_detector<image_scanner_type> detector_tsign;
	dlib::matrix <dlib::bgr_pixel>cimg;

	vector<dlib::rectangle> dets_tlight;
	vector<dlib::rectangle> dets_tsign;

	dlib::deserialize("tlight3_detector.svm") >> detector_tlight;
	dlib::deserialize("tsign_detector.svm") >> detector_tsign;
	
	while(1)
	{

		sem_wait(&img_sync);	
	
		dlib::assign_image(cimg,dlib::cv_image<dlib::rgb_pixel>(img));
		dlib::pyramid_up(cimg);	
		
		scanner_tl.set_detection_window_size(80, 170); 
		scanner_ts.set_detection_window_size(80, 80); 
		scanner_tl.set_max_pyramid_levels(1);	
		scanner_ts.set_max_pyramid_levels(1); 
		
		detect_color = tlight_msg_handler(img);

		if(detect_color)
		{	

			dets_tlight = detector_tlight(cimg);

			if(dets_tlight.size())
			{
				cout<<"detected traffic light RED"<<endl;
				red_sign_on  = 2;
			}

			detect_color = 0;
		}

		dets_tsign = detector_tsign(cimg);

		if(dets_tsign.size())
		{
			cout<<"deteced child"<<endl;
			int ret = system("mpg123 child_sign_voice.mp3");
			child_sign_on = 1;
		}
	}
}
void*msg_send(void*arg)
{
	int connSock=*(int*)arg;

	while(1)
	{
		buf= red_sign_on | child_sign_on; // 0 fast, 1 slow, 2 stop, 3 slow and stop

		if(send(connSock, &buf, sizeof(buf), 0) < 0) 
		{
			perror("send to traffic server failed");
		}

		red_sign_on = 0;
		child_sign_on = 0;
	}
}


void create_msg_box(std::vector<dlib::rectangle> &dets, dlib::rectangle &r)
{

	if(dets.size())
	{
		r.left()=dets[0].left();
		r.bottom()=dets[0].bottom();
		r.right()=dets[0].right();
		if(dets[0].top() > 0)
			r.top()=dets[0].top();
		else
			r.top()=0;
	}

}

int tlight_msg_handler(Mat &img)
{
	int i = 0;
	int red_positive;
		
	red_positive = hsv_handler(img);

	if(red_positive%2 != 0)
	{
		cout<<endl;
		cout<<"redsign == Stop"<<' '<<i<<endl;
		cout<<endl;
		i++;
		return 2;
	}
	else
	{
		cout<<endl;
		cout<<"no red sign == Go"<<endl;
		if(red_positive)
			return 1;
	}

	return 0;
}


int tsign_msg_handler(Mat &img, int dets, int connSock, dlib::rectangle &r, char*distance_msg)
{
	
	float distance;
	int i= 0;
	int red_positive;
		
	if(dets)
	{
		distance=dist_detect_ts(r);
		sprintf(distance_msg,"  DISTANCE : %.2fCM\n",distance);
		
		if(distance < 20)
		{
			child_sign_on = 1;
			cout<<"Stop"<<' '<<i<<endl;
			i++;
		}
	}

	sprintf(distance_msg,"  detected sign : %d\n",dets);

	return 0;
}


float dist_detect_tl(dlib::rectangle &r)
{
	//float v = (r.bottom() - r.top())/2+r.top();
	float v = (r.right() - r.left())<<2;
	float distance = 14/tan ( (-3.1) + atan( (v- 119.8) / 332.3 ) );
	cout<< "distance = "<<distance<<"cm"<<endl;
	
	return distance;
}

float dist_detect_ts(dlib::rectangle &r)
{
	//float v = (r.bottom() - r.top())/2+r.top();
	float v = ((r.right() - r.left())<<2)/3;
	float distance = 14/tan ( (-3.1) + atan( (v- 119.8) / 332.3 ) );
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

	cvtColor(img_for_detect, img_hsv, COLOR_RGB2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::RED_HL,HSV_CONST::RED_SL,HSV_CONST::RED_VL),\
		Scalar(HSV_CONST::RED_HH,HSV_CONST::RED_SH,HSV_CONST::RED_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 8, CV_32S);  

	printf("detected reds : %d\n",numOfLables-1);

	return numOfLables;
}

int green_detect(Mat &img_for_detect)
{
	Mat img_binary, img_hsv;

	cvtColor(img_for_detect, img_hsv, COLOR_RGB2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::GREEN_HL,HSV_CONST::GREEN_SL,HSV_CONST::GREEN_VL),\
		Scalar(HSV_CONST::GREEN_HH,HSV_CONST::GREEN_SH,HSV_CONST::GREEN_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 4, CV_32S);  

	printf("detected greens : %d\n",numOfLables-1);

	return numOfLables;
}
