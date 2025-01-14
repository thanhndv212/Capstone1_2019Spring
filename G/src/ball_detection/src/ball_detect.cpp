#include "ball_detection/ball_detect.h"

using namespace cv;
using namespace std;
using namespace sensor_msgs;
using namespace message_filters;

BallDetectNode::BallDetectNode():  nh_private_ ( "~" ) {

    pub = nh.advertise<core_msgs::ball_position>("/ball_position", 100);

    reconfigureServer_ = new dynamic_reconfigure::Server<ball_detection::BallDetectionConfig> ( ros::NodeHandle ( "~" ) );
    reconfigureFnc_ = boost::bind ( &BallDetectNode::callbackConfig, this,  _1);
    reconfigureServer_->setCallback ( reconfigureFnc_ );

    message_filters::Subscriber<Image> color_sub(nh, "camera/color/image_raw", 1);
    message_filters::Subscriber<Image> depth_sub(nh, "camera/aligned_depth_to_color/image_raw", 1);
    TimeSynchronizer<Image, Image> sync(color_sub, depth_sub, 10);

    sync.registerCallback(boost::bind(&BallDetectNode::imageCallback, this, _1, _2));

    ros::spin();
}


void BallDetectNode::callbackConfig (ball_detection::BallDetectionConfig &_config){
    config_ = _config;
}

void BallDetectNode::imageCallback(const sensor_msgs::ImageConstPtr& msg_color, const sensor_msgs::ImageConstPtr& msg_depth)
{

    buffer_color = cv::Mat(480,640,CV_8UC3);
    buffer_depth = cv::Mat(480,640,CV_16UC1);

    try
    {
        buffer_color = cv_bridge::toCvShare(msg_color, "bgr8")->image;  //transfer the image data into buffer
        buffer_depth = cv_bridge::toCvShare(msg_depth, "16UC1")->image;
    }
    catch (cv_bridge::Exception& e)
    {
         ROS_ERROR("Could not convert from '%s' to 'bgr8'.", msg_color->encoding.c_str());
         ROS_ERROR("Could not convert from '%s' to '16UC1'.", msg_depth->encoding.c_str());
    }

    balls_info detected_balls = ball_detect(); //proceed ball detection
    pub_msgs(detected_balls);

}


balls_info BallDetectNode::ball_detect(){
    Mat color_frame, depth_frame, bgr_frame, hsv_frame, hsv_frame_red, hsv_frame_red1, hsv_frame_red2, hsv_frame_blue, hsv_frame_green, \
            hsv_frame_red_canny, hsv_frame_blue_canny, hsv_frame_green_canny, result; //declare matrix for frames and result

    //Copy buffer image to frame. If the size of the orignal image(cv::Mat buffer) is 320x240, then resize the image to save (cv::Mat frame) it to 640x480
    if(buffer_color.size().width==320){
        cv::resize(buffer_color, color_frame, cv::Size(640, 480));
    }
    else{
        color_frame = buffer_color;
    }

    if(buffer_depth.size().width==320){
        cv::resize(buffer_depth, depth_frame, cv::Size(640, 480));
    }
    else{
        depth_frame = buffer_depth;
    }

    //declare matrix for calibrated frame
    Mat intrinsic = Mat(3,3, CV_32FC1); //set intrinsic matrix as 3x3 matrix with 32bit float 1 channel
    Mat distCoeffs; //declare matrix distCoeffs
    intrinsic = Mat(3, 3, CV_32F, intrinsic_data); //put intrinsic_data to intrinsic matrix
    distCoeffs = Mat(1, 5, CV_32F, distortion_data); //put distortion_data to disCoeffs matrix
    vector<Vec4i> hierarchy_r; //declare hierachy_r as 4 element vector(line)
    vector<Vec4i> hierarchy_b; //declare hierachy_b as 4 element vector(line)
    vector<Vec4i> hierarchy_g; //declare hierachy_g as 4 element vector(line)
    vector<vector<Point> > contours_r; //declare contours_r as point vector
    vector<vector<Point> > contours_b; //declare contours_b as point vector
    vector<vector<Point> > contours_g; //declare contours_b as point vector

    VideoCapture cap; //get image from video, May be changed to 0 for NUC

    //undistort(color_frame, calibrated_frame, intrinsic, distCoeffs);
    //result = calibrated_frame.clone();
    //calibrated_frame = color_frame.clone();

    Mat& calibrated_frame = color_frame; // since an input image is undistorted already, WE JUST USE IT.
    result = calibrated_frame.clone();

    /* step 1: blur it */
    medianBlur(calibrated_frame, calibrated_frame, 3);

    /* step 2: convert bgr colorspace to hsv and apply threshold filter */
    cvtColor(calibrated_frame, hsv_frame, cv::COLOR_BGR2HSV);
    // From now on, all matrices are 8UC1 encoded.
    inRange(hsv_frame,Scalar(config_.low_h_r,config_.low_s_r,config_.low_v_r),Scalar(config_.high_h_r,config_.high_s_r,config_.high_v_r),hsv_frame_red1);
    inRange(hsv_frame,Scalar(config_.low_h2_r,config_.low_s_r,config_.low_v_r),Scalar(config_.high_h2_r,config_.high_s_r,config_.high_v_r),hsv_frame_red2);
    inRange(hsv_frame,Scalar(config_.low_h_b,config_.low_s_b,config_.low_v_b),Scalar(config_.high_h_b,config_.high_s_b,config_.high_v_b),hsv_frame_blue);
    inRange(hsv_frame,Scalar(config_.low_h_g,config_.low_s_g,config_.low_v_g),Scalar(config_.high_h_g,config_.high_s_g,config_.high_v_g),hsv_frame_green);
    addWeighted(hsv_frame_red1, 1.0, hsv_frame_red2, 1.0, 0.0,hsv_frame_red); //merge two frames(hsv_frame_red1, hsv_frame_red2) ratio of 1:1 add scalar 0, output to hsv_frame_red


    /* step 3: apply morphOps function to the colorwise-filtered images */
    //morphOps(hsv_frame_red); //apply function morphOps to hsv_frame_red
    //morphOps(hsv_frame_blue); //apply function morphOps to hsv_frame_blue


    Mat hsv_red_result, hsv_blue_result, hsv_green_result;
    bitwise_and(calibrated_frame, calibrated_frame, hsv_red_result, hsv_frame_red); // input1, input2, result, mask (hsv_frame is a return of inRange, which prints out binary array
    bitwise_and(calibrated_frame, calibrated_frame, hsv_blue_result, hsv_frame_blue); // input1, input2, result, mask (hsv_frame is a return of inRange, which prints out binary array
    bitwise_and(calibrated_frame, calibrated_frame, hsv_green_result, hsv_frame_green); // input1, input2, result, mask (hsv_frame is a return of inRange, which prints out binary array


    /* step 4: apply gaussian blur to the binary image ;  */
    GaussianBlur(hsv_frame_red, hsv_frame_red, cv::Size(9, 9),2, 2); //gaussian blur; input: hsv_frame_red, output: hsv_frame_red_blur, gaussian kernel
    GaussianBlur(hsv_frame_blue, hsv_frame_blue, cv::Size(9,9), 2, 2); //gaussian blur; input: hsv_frame_blue, output: hsv_frame_blue_blur, gaussian kernel
    GaussianBlur(hsv_frame_green, hsv_frame_green, cv::Size(9,9), 2, 2); //gaussian blur; input: hsv_frame_green, output: hsv_frame_green_blur, gaussian kernel

    /* step 5: edge detection */
    Canny(hsv_frame_red, hsv_frame_red_canny, lowThreshold_r,lowThreshold_r*ratio_r, kernel_size_r);
    Canny(hsv_frame_blue, hsv_frame_blue_canny, lowThreshold_b, lowThreshold_b*ratio_b, kernel_size_b);
    Canny(hsv_frame_green, hsv_frame_green_canny, lowThreshold_g, lowThreshold_g*ratio_g, kernel_size_g);

    // dilate detected canny edges to visualize them easily
    dilate(hsv_frame_red_canny, hsv_frame_red_canny, Mat(), Point(-1, -1));
    dilate(hsv_frame_blue_canny, hsv_frame_blue_canny, Mat(), Point(-1, -1));
    dilate(hsv_frame_green_canny, hsv_frame_green_canny, Mat(), Point(-1, -1));

    /* step 6: Detect contours. Note that each contour is stored as a vector of points (e.g. std::vector<std::vector<cv::Point> >).
    the number of detected contours : n, its number of pixels: p , then the variable 'contours_r's size become n by p. (i.e. contours_r[n-1][p-1]1)
    */
    findContours(hsv_frame_red_canny, contours_r, hierarchy_r,RETR_CCOMP, CHAIN_APPROX_SIMPLE, Point(0, 0)); //find contour from hsv_frame_red_canny to contours_r, optional output vector(containing image topology) hierarchy_r, contour retrieval mode: RETER_CCOMP, contour approximationmethod: CHAIN_APPROX_SIMPLE, shift point (0,0)(don't shift the point)
    findContours(hsv_frame_blue_canny, contours_b, hierarchy_b,RETR_CCOMP, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(hsv_frame_green_canny, contours_g, hierarchy_g,RETR_CCOMP, CHAIN_APPROX_SIMPLE, Point(0, 0));


    /* step 7: With detected contours above, estimate each contour's center and radius. */
    vector<vector<Point> > contours_r_poly( contours_r.size() );
    vector<vector<Point> > contours_b_poly( contours_b.size() );
    vector<vector<Point> > contours_g_poly( contours_g.size() );
    vector<Point2f>center_r2f( contours_r.size() );
    vector<Point2f>center_b2f( contours_b.size() );
    vector<Point2f>center_g2f( contours_g.size() );

    vector<float>radius_r2f( contours_r.size() ); //set radius_r as float type vector size of contours_r
    vector<float>radius_b2f( contours_b.size() ); //set radius_b as float type vector size of contours_b
    vector<float>radius_g2f( contours_g.size() ); //set radius_g as float type vector size of contours_g

    for( size_t i = 0; i < contours_r.size(); i++ ){
        approxPolyDP( contours_r[i], contours_r_poly[i], 3, true ); //approximate contours_r[i] to a smoother polygon and put output in contours_r_poly[i] with approximation accuracy 3 (pixelwise distance between the original and approximated point)
        minEnclosingCircle( contours_r_poly[i], center_r2f[i], radius_r2f[i] ); //get position of the polygon's center and its radius from minimum enclosing circle from contours_r_poly[i]
    }
    for( size_t i = 0; i < contours_b.size(); i++ ){
        //run the loop while size_t type i from 0 to size of contours_b-1 size by increasing i 1
        approxPolyDP( contours_b[i], contours_b_poly[i], 3, true );
        minEnclosingCircle( contours_b_poly[i], center_b2f[i], radius_b2f[i] );
    }
    for( size_t i = 0; i < contours_g.size(); i++ ){
        //run the loop while size_t type i from 0 to size of contours_g-1 size by increasing i 1
        approxPolyDP( contours_g[i], contours_g_poly[i], 3, true );
        minEnclosingCircle( contours_g_poly[i], center_g2f[i], radius_g2f[i] );
    }

    remove_trashval(center_r2f, radius_r2f, iMin_tracking_ball_size);
    remove_trashval(center_b2f, radius_b2f, iMin_tracking_ball_size);
    remove_trashval(center_g2f, radius_g2f, iMin_tracking_ball_size);


    /* Step 8: Push data into the return value. */
    balls_info vals;

    vals.num_r = (int)(center_r2f.size());
    vals.num_b = (int)(center_b2f.size());
    vals.num_g = (int)(center_g2f.size());


    for (int i=0 ; i<vals.num_r; i++)
    {
        vals.center_r.push_back( cv::Point2i( (int)(center_r2f[i].x), (int)(center_r2f[i].y) ) );
        vals.radius_r.push_back( (int)(radius_r2f[i]));

        int x = vals.center_r[i].x;
        int y = vals.center_r[i].y;

        vals.distance_r.push_back(calibrate_rangeinfo(lookup_range(y,x,depth_frame)));
    }
    for (int i=0 ; i<vals.num_b; i++)
    {
        vals.center_b.push_back( cv::Point2i( (int)(center_b2f[i].x), (int)(center_b2f[i].y) ) );
        vals.radius_b.push_back( (int)(radius_b2f[i]));

        int x = vals.center_b[i].x;
        int y = vals.center_b[i].y;

        vals.distance_b.push_back(calibrate_rangeinfo(lookup_range(y,x,depth_frame)));
    }

    for (int i=0 ; i<vals.num_g; i++)
    {
        vals.center_g.push_back( cv::Point2i( (int)(center_g2f[i].x), (int)(center_g2f[i].y) ) );
        vals.radius_g.push_back( (int)(radius_g2f[i]));

        int x = vals.center_g[i].x;
        int y = vals.center_g[i].y;

        vals.distance_g.push_back(calibrate_rangeinfo(lookup_range(y,x,depth_frame)));
    }

    return vals;

}


void BallDetectNode::pub_msgs(balls_info &ball_information){

    unsigned short num_r = static_cast<unsigned short>(ball_information.num_r);
    unsigned short num_b = static_cast<unsigned short>(ball_information.num_b);
    unsigned short num_g = static_cast<unsigned short>(ball_information.num_g);
    vector<Point2i>center_r = ball_information.center_r;
    vector<Point2i>center_b = ball_information.center_b;
    vector<Point2i>center_g = ball_information.center_g;
    vector<int>radius_r = ball_information.radius_r;
    vector<int>radius_b = ball_information.radius_b;
    vector<int>radius_g = ball_information.radius_g;
    vector<short int>distance_r = ball_information.distance_r;
    vector<short int>distance_b = ball_information.distance_b;
    vector<short int>distance_g = ball_information.distance_g;

    /*** declare msg_pub which will be passed to other nodes ***/
    core_msgs::ball_position msg_pub;

    msg_pub.red_size = num_r;
    msg_pub.blue_size = num_b;
    msg_pub.green_size = num_g;
    msg_pub.red_x.resize(num_r);
    msg_pub.red_y.resize(num_r);
    msg_pub.red_distance.resize(num_r);
    msg_pub.blue_x.resize(num_b);
    msg_pub.blue_y.resize(num_b);
    msg_pub.blue_distance.resize(num_b);
    msg_pub.green_x.resize(num_g);
    msg_pub.green_y.resize(num_g);
    msg_pub.green_distance.resize(num_g);

    for( size_t i = 0; i< center_r.size(); i++ ){
        vector<float> ball_position_r; //declare float vector named ball_position_r
        ball_position_r = pixel2point_depth(center_r[i], distance_r[i]);
        ball_position_r = transform_coordinate(ball_position_r);

        float isx = ball_position_r[0];
        float isy = ball_position_r[1];
        float isz = ball_position_r[2];

        msg_pub.red_x[i]=(short int)(1000*ball_position_r[0]);
        msg_pub.red_y[i]=(short int)(1000*ball_position_r[1]);
        msg_pub.red_distance[i] = distance_r[i];

    }

    for( size_t i = 0; i< center_b.size(); i++ ){ //run the loop while size_t type i from 0 to size of contours_b-1 size by increasing i 1
        vector<float> ball_position_b; //declare float vector named ball_position_b
        ball_position_b = pixel2point_depth(center_b[i], distance_b[i]);
        ball_position_b = transform_coordinate(ball_position_b);

        float isx = ball_position_b[0];
        float isy = ball_position_b[1];
        float isz = ball_position_b[2];


        msg_pub.blue_x[i]=(short int)(1000*ball_position_b[0]);
        msg_pub.blue_y[i]=(short int)(1000*ball_position_b[1]);
        msg_pub.blue_distance[i] = distance_b[i];

    }

    for( size_t i = 0; i< center_g.size(); i++ ){ //run the loop while size_t type i from 0 to size of contours_g-1 size by increasing i 1
        vector<float> ball_position_g; //declare float vector named ball_position_g
        ball_position_g = pixel2point_depth(center_g[i], distance_g[i]);
        ball_position_g = transform_coordinate(ball_position_g);

        float isx = ball_position_g[0];
        float isy = ball_position_g[1];
        float isz = ball_position_g[2];


        msg_pub.green_x[i]=(short int)(1000*ball_position_g[0]);
        msg_pub.green_y[i]=(short int)(1000*ball_position_g[1]);
        msg_pub.green_distance[i] = distance_g[i];
    }

    pub.publish(msg_pub);

}


vector<float> BallDetectNode::pixel2point_depth(Point2i pixel_center, int distance){
    vector<float> position;

    float u, v, Xc, Yc, Zc;

    u = (static_cast<float>(pixel_center.x)-intrinsic_data[2])/intrinsic_data[0];
    v = (static_cast<float>(pixel_center.y)-intrinsic_data[5])/intrinsic_data[4];

    Zc= 1/sqrt(pow(u,2.0) + pow(v,2.0) + 1.0)*distance/1000; //compute Z value of the ball center
    Xc=u*Zc ; //calculate real x position from camera coordinate
    Yc=v*Zc ; //calculate real y position from camera coordinate


    Xc=roundf(Xc * 1000) / 1000;
    Yc=roundf(Yc * 1000) / 1000;
    Zc=roundf(Zc * 1000) / 1000;
    position.push_back(Xc);
    position.push_back(Yc);
    position.push_back(Zc);
    return position;
}


std::vector<float> BallDetectNode::pixel2point(cv::Point2i pixel_center, int pixel_radius){
    //get center of ball in 2d image plane and transform it into ball position in 3d camera coordinate
    vector<float> position;
    float x, y, u, v, Xc, Yc, Zc;
    x = pixel_center.x;
    y = pixel_center.y;

    u = (x-intrinsic_data[2])/intrinsic_data[0];
    v = (y-intrinsic_data[5])/intrinsic_data[4];

    Yc= (intrinsic_data[0]*fball_radius)/(2*(float)pixel_radius) ; //compute value of the ball center
    Xc= u*Yc ; //calculate real x position from camera coordinate
    Zc= v*Yc ; //calculate real y position from camera coordinate
    Xc=roundf(Xc * 1000) / 1000; //make Xc to 4digit
    Yc=roundf(Yc * 1000) / 1000; //make Yc to 4digit
    Zc=roundf(Zc * 1000) / 1000; //make Zc to 4digit
    position.push_back(Xc);
    position.push_back(Yc);
    position.push_back(Zc);
    return position;
}


vector<float> BallDetectNode::transform_coordinate( vector<float> input ){
    vector<float> output;

    float x = rotation[0]*input[0] + rotation[1]*input[1] + rotation[2]*input[2] + translation[0];
    float y = rotation[3]*input[0] + rotation[4]*input[1] + rotation[5]*input[2] + translation[1];
    float z = rotation[6]*input[0] + rotation[7]*input[1] + rotation[8]*input[2] + translation[2];

    output.push_back(x);
    output.push_back(y);
    output.push_back(z);

    return output;

}


void BallDetectNode::morphOps(Mat &thresh){ //dilate and erode image
    //create structuring element that will be used to "dilate" and "erode" image.
    //the element chosen here is a 3px by 3px rectangle
    Mat erodeElement = getStructuringElement( MORPH_RECT,Size(3,3));
    //dilate with larger element so make sure object is nicely visible
    Mat dilateElement = getStructuringElement( MORPH_RECT,Size(8,8));
    erode(thresh,thresh,erodeElement);
    //erode(thresh,thresh,erodeElement);
    dilate(thresh,thresh,dilateElement);
    //dilate(thresh,thresh,dilateElement);
}


void BallDetectNode::remove_trashval(vector<Point2f> &center, vector<float> &radius, int pixel_radius){

    vector<Point2f> temp_center = center;
    vector<float> temp_radius = radius;

    size_t i = 0;
    while (i < temp_radius.size()){ //when there’s some contours in the field of view
        bool something = true;
        //assign dummy Boolean
        for (size_t j = 0; j < temp_radius.size() ; j++){
            if ( (i!=j&&(norm(temp_center[i] - temp_center[j]) < temp_radius[j])) || (temp_radius[i] < pixel_radius) ){
                temp_center.erase(temp_center.begin()+i); //remove ith element from vector
                temp_radius.erase(temp_radius.begin()+i);
                something = false;
                break;
            }
        }
        if(something){
            i++;
        }
    }

    radius = temp_radius;
    center = temp_center;

}


short int BallDetectNode::calibrate_rangeinfo(short int x){
    short int y = 1/config_.a * (x - config_.b);
    return abs(y);

}


short int BallDetectNode::lookup_range(int y, int x, cv::Mat& range_frame){


    if(range_frame.at<short int>(y,x) < 1000) return range_frame.at<short int>(y,x)+(short int)(fball_radius*1000); // 1000 [mm] denotes the threshold of the adaptive range lookup method.
    else {

    Mat roi = range_frame(Rect(max(0,x-1),max(0,y-1),min(range_frame.cols-x,3),min(range_frame.rows-y,3)));

    short int sum = 0;
    int count = 0;

    for ( int i=0;i<roi.rows;i++) {
        for ( int j=0;j<roi.cols;j++) {
            if( roi.at<short int>(i,j) != (short int)(0)){
                sum += roi.at<short int>(i,j);
                count ++;
            }
        }
    }

    short int d;
    if(count == (short int)(0)) d = 0;
    else d = sum/(short int)(count);

    d += (short int)(fball_radius*1000);
    return d;

    }
};


// functions which change data type
string intToString(int n){
    stringstream s;
    s << n;
    return s.str();
}


string floatToString(float f){
    ostringstream buffer;
    buffer << f;
    return buffer.str();
}


string type2str(int type) {
    /* usage:
    string ty =  type2str( M.type() );
    printf("Matrix: %s %dx%d \n", ty.c_str(), M.cols, M.rows );
    */

    string r;

    uchar depth = type & CV_MAT_DEPTH_MASK;
    uchar chans = 1 + (type >> CV_CN_SHIFT);

    switch ( depth ) {
        case CV_8U:  r = "8U"; break;
        case CV_8S:  r = "8S"; break;
        case CV_16U: r = "16U"; break;
        case CV_16S: r = "16S"; break;
        case CV_32S: r = "32S"; break;
        case CV_32F: r = "32F"; break;
        case CV_64F: r = "64F"; break;
        default:     r = "User"; break;
    }

    r += "C";
    r += (chans+'0');

    return r;
}


int main(int argc, char **argv)
{
    ros::init(argc, argv, "ball_detect_range_assisted");

    BallDetectNode ball_detect_node;

    return 0;
}
