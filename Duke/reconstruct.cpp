#include "reconstruct.h"
#include <QMessageBox>

Reconstruct::Reconstruct(int numOfCams_,QString path)
{
    numOfCams = numOfCams_;
    mask = NULL;
    decRows = NULL;
    decCols = NULL;
    points3DProjView = NULL;
    autoContrast_ = false;
    saveAutoContrast_ = false;
    cameras = new  VirtualCamera[2];//生成virtualcamera的两个实例，保存在数组cameras[2]
    camsPixels = new cv::vector<cv::Point>*[2];
    calibFolder = new QString[2];
    scanFolder = new QString[2];
    imgPrefix = new QString[2];
    pathSet = false;
    for(int i=0; i< 2; ++i)
    {
        QString pathI;
        if(i==0){
            pathI = path + "/scan/left/";//Load Images for reconstruction
        }
        else{
            pathI = path + "/scan/right/";//输入path为projectPath
        }
        camsPixels[i] = NULL;
        scanFolder[i] = pathI;
        if(i==0)
            imgPrefix[i] = "L";
        else
            imgPrefix[i] = "R";
    }
    imgSuffix = ".png";//这里暂时认为图片后缀为.png
}
Reconstruct::~Reconstruct()
{
    unloadCamImgs();
    if(points3DProjView)
        delete points3DProjView ;
}

void Reconstruct::enableSavingAutoContrast()
{
    saveAutoContrast_ = true;
}

void Reconstruct::disableSavingAutoContrast()
{
    saveAutoContrast_ = false;
}

void Reconstruct::enableRaySampling()
{
    raySampling_ = true;
}

void Reconstruct::disableRaySampling()
{
    raySampling_ = false;
}

void Reconstruct::setBlackThreshold(int val)
{
    blackThreshold = val;
}

void Reconstruct::setWhiteThreshold(int val)
{
    whiteThreshold = val;
}

void Reconstruct::decodePaterns()
{
    int w = cameraWidth;
    int h = cameraHeight;
    cv::Point projPixel;
    for(int i=0; i<w; i++)
    {
        for(int j=0; j<h; j++)
        {
            if(mask.at<uchar>(j,i))//if the pixel is not shadow reconstruct
            {    
                bool error = getProjPixel(i, j, projPixel);//get the projector pixel for camera (i,j) pixel
                if(error)
                {
                    mask.at<uchar>(j,i)=0;
                    continue;
                }
                camPixels[ac(projPixel.x,projPixel.y)].push_back(cv::Point(i, j));
            }
        }
    }
}

bool Reconstruct::loadCameras()//Load calibration data
{
    bool loaded;
    for(int i=0; i<numOfCams; i++)
    {
        QString path;
        path = calibFolder[i];
        path += "cam_matrix.txt";
        loaded = cameras[i].loadCameraMatrix(path);//defined in visualcamera
        if(!loaded){
            break;
        }
        path = calibFolder[i];
        path += "cam_distortion.txt";
        cameras[i].loadDistortion(path);//注意loaddistortion方法加载一个5X1矩阵，而不是说明书里的3X1

        path = calibFolder[i];
        path += "cam_rotation_matrix.txt";
        cameras[i].loadRotationMatrix(path);

        path = calibFolder[i];
        path += "cam_trans_vectror.txt";
        cameras[i].loadTranslationVector(path);

        cameras[i].height = 0;
        cameras[i].width = 0;
    }
    return loaded;
}

bool Reconstruct::loadCamImgs(QString folder, QString prefix, QString suffix)//load camera images
{
    cv::Mat tmp;
    if(!camImgs.empty())
        unloadCamImgs();
    for(int i=0; i<numberOfImgs; i++)
    {
        QString path;
        path = folder;//这里folder要达到left/right一层
        path += prefix + QString::number(i,10) + suffix;//图片加前后缀
        tmp.release();
        //std::string cstr;
        //cstr = std::string((const char *)path.toLocal8Bit());
        //tmp = cv::imread(cstr);
        tmp = cvLoadImage(path.toLocal8Bit(),0);

        if(tmp.empty())
        {
            QMessageBox::warning(NULL,"Warning","Images not found!");
            break;
        }
        else{
            if(autoContrast_)//auto contrast
            {
                Utilities::autoContrast(tmp,tmp);
                if(saveAutoContrast_)
                {
                    QString p;
                    p = savePath_ + "/AutoContrastSave/" + QString::number(i+1,10) + suffix;
                    std::string cstr;
                    cstr = std::string((const char *)p.toLocal8Bit());
                    cv::imwrite(cstr,tmp);
                }
            }
            if(i==0)
            {
                color = tmp;
            }
            //cv::cvtColor(tmp, tmp, CV_BGR2GRAY);//这里暂时注释掉，由于源程序处理彩色图像，而这里是灰度图像，cvtColor用来处理彩色图像，故直接用存在问题
            camImgs.push_back(tmp);
        }
        if(camera->width == 0)
        {
            camera->height = camImgs[0].rows;
            camera->width = camImgs[0].cols;
        }
    }
    return !tmp.empty();
}

void Reconstruct::unloadCamImgs()//unload camera images
{
    if(camImgs.size())
    {
        for(int i=0; i<numberOfImgs; i++)
        {
            camImgs[i].release();
        }
    }
    color.release();
    camImgs.clear();
}

void Reconstruct::computeShadows()
{
    int w = camera->width;
    int h = camera->height;
    mask.release();
    mask = cv::Mat(h,w,CV_8U,cv::Scalar(0));
    for(int i=0; i<w; i++)
    {
        for(int j=0; j<h; j++)
        {
            float blackVal, whiteVal;
            blackVal  = (float) Utilities::matGet2D( camImgs[1], i, j);
            whiteVal  = (float) Utilities::matGet2D( camImgs[0], i, j);
            if(whiteVal - blackVal > blackThreshold)
            {
                Utilities::matSet2D(mask,i,j,1);
            }
            else
            {
                Utilities::matSet2D(mask,i,j,0);
            }
        }
    }
}

bool Reconstruct::runReconstruction()
{
    bool runSucess = false;
    GrayCodes grays(proj_w,proj_h);//proj_w proj_h get var getparameter
    numOfColBits = grays.getNumOfColBits();
    numOfRowBits = grays.getNumOfRowBits();
    numberOfImgs = grays.getNumOfImgs();//以上clear

    for(int i=0; i < numOfCams; i++)
    {
        cameras[i].position = cv::Point3f(0,0,0);//findProjectorCenter();
        cam2WorldSpace(cameras[i],cameras[i].position);
        camera = &cameras[i];
        camsPixels[i] = new cv::vector<cv::Point>[proj_h*proj_w];
        camPixels = camsPixels[i];
        runSucess = loadCamImgs(scanFolder[i],imgPrefix[i],imgSuffix);
        if(!runSucess)
            break;
        else{
            colorImgs.push_back(cv::Mat());
            colorImgs[i] = color;//在loadCamImgs中生成了color
            computeShadows();
            decodePaterns();
            unloadCamImgs();
        }
    }
    if(runSucess){
        points3DProjView = new PointCloudImage( proj_w, proj_h , true ); //reconstruct
        for(int i = 0; i < numOfCams; i++)
        {
            for(int j=i+1; j< numOfCams; j++)
                triangulation(camsPixels[i],cameras[i],camsPixels[j],cameras[j], i, j);
        }
    }
    return runSucess;
}

void Reconstruct::cam2WorldSpace(VirtualCamera cam, cv::Point3f &p)//convert a point from camera to world space
{
    cv::Mat tmp(3,1,CV_32F);
    cv::Mat tmpPoint(3,1,CV_32F);
    tmpPoint.at<float>(0) = p.x;
    tmpPoint.at<float>(1) = p.y;
    tmpPoint.at<float>(2) = p.z;
    tmp = -cam.rotationMatrix.t() * cam.translationVector ;
    tmpPoint = cam.rotationMatrix.t() * tmpPoint;
    p.x = tmp.at<float>(0) + tmpPoint.at<float>(0);
    p.y = tmp.at<float>(1) + tmpPoint.at<float>(1);
    p.z = tmp.at<float>(2) + tmpPoint.at<float>(2);
}

bool Reconstruct::getProjPixel(int x, int y, cv::Point &p_out)//for a (x,y) pixel of the camera returns the corresponding projector pixel
{
    cv::vector<bool> grayCol;
    cv::vector<bool> grayRow;
    bool error = false;
    //int error_code = 0;
    int xDec,yDec;
    //prosses column images
    for(int count=0; count<numOfColBits; count++)
    {
        //get pixel intensity for regular pattern projection and it's inverse
        double val1, val2;
        val1 = Utilities::matGet2D(camImgs[count * 2 + 2   ],x,y);
        val2 = Utilities::matGet2D(camImgs[count * 2 + 2 +1],x,y);
        //check if intensity deference is in a valid rage
        if(abs(val1-val2) < whiteThreshold )
            error=true;
        //determine if projection pixel is on or off
        if(val1>val2)
            grayCol.push_back(1);
        else
            grayCol.push_back(0);
    }
    xDec = GrayCodes::grayToDec(grayCol);
    //prosses row images
    for(int count=0; count < numOfRowBits; count++)
    {
        double val1, val2;
        val1 = Utilities::matGet2D(camImgs[count*2+2+numOfColBits*2],x,y);
        val2 = Utilities::matGet2D(camImgs[count*2+2+numOfColBits*2+1],x,y);
        if(abs(val1-val2) < whiteThreshold )  //check if the difference between the values of the normal and it's inverce projection image is valid
            error = true;
        if(val1 > val2)
            grayRow.push_back(1);
        else
            grayRow.push_back(0);
    }
    //decode
    yDec = GrayCodes::grayToDec(grayRow);

    if((yDec > proj_h || xDec > proj_w))
    {
        error = true;
    }

    p_out.x = xDec;
    p_out.y = yDec;

    return error;
}

void Reconstruct::setCalibPath(QString folder,QString prefix,QString suffix, int cam_no )
{
    calibFolder[cam_no] = folder;//projectPath+"/calib/left/"或projectPath+"/calib/right/"
    imgPrefix[cam_no] = prefix;//前缀，分别为L和R
    imgSuffix = suffix;//后缀，暂定为.png
    pathSet = true;
}

void Reconstruct::triangulation(cv::vector<cv::Point> *cam1Pixels, VirtualCamera camera1, cv::vector<cv::Point> *cam2Pixels, VirtualCamera camera2, int cam1index, int cam2index)
{
    int w = proj_w;
    int h = proj_h;
    //start reconstraction
    int load=0;
    //reconstraction for every projector pixel
    for(int i=0; i<w; i++)
    {
        for(int j=0; j<h; j++)
        {
            if(load != (int) (((j+(float)i*h)/((float)w*h))*100))
            {
                load =  (int) (((j+(float)i*h)/((float)w*h))*100);
            }
            cv::vector<cv::Point> cam1Pixs,cam2Pixs;
            cam1Pixs = cam1Pixels[ac(i,j)];
            cam2Pixs = cam2Pixels[ac(i,j)];
            cv::Point3f reconstructedPoint(0,0,0);
            if( cam1Pixs.size() == 0 || cam2Pixs.size() == 0)
                continue;
            cv::Vec3f color1,color2;
            for(int c1=0; c1 < cam1Pixs.size(); c1++)
            {
                cv::Point2f camPixelUD = Utilities::undistortPoints(cv::Point2f(cam1Pixs[c1].x,cam1Pixs[c1].y),camera1);//camera 3d point p for (i,j) pixel
                cv::Point3f cam1Point = Utilities::pixelToImageSpace(camPixelUD,camera1); //convert camera pixel to image space
                cam2WorldSpace(camera1, cam1Point);

                cv::Vec3f ray1Vector = (cv::Vec3f) (camera1.position - cam1Point); //compute ray vector
                Utilities::normalize(ray1Vector);

                //get pixel color for the first camera view
                color1 = Utilities::matGet3D( colorImgs[cam1index], cam1Pixs[c1].x, cam1Pixs[c1].y);

                for(int c2=0; c2 < cam2Pixs.size(); c2++)
                {
                    camPixelUD = Utilities::undistortPoints(cv::Point2f(cam2Pixs[c2].x,cam2Pixs[c2].y),camera2);//camera 3d point p for (i,j) pixel

                    cv::Point3f cam2Point = Utilities::pixelToImageSpace(camPixelUD,camera2); //convert camera pixel to image space
                    cam2WorldSpace(camera2, cam2Point);

                    cv::Vec3f ray2Vector = (cv::Vec3f) (camera2.position - cam2Point); //compute ray vector
                    Utilities::normalize(ray2Vector);

                    cv::Point3f interPoint;

                    bool ok = Utilities::line_lineIntersection(camera1.position,ray1Vector,camera2.position,ray2Vector,interPoint);

                    if(!ok)
                        continue;

                    //get pixel color for the second camera view
                    color2 = Utilities::matGet3D( colorImgs[cam2index], cam2Pixs[c2].x, cam2Pixs[c2].y);
                    points3DProjView->addPoint(i,j,interPoint, (color1 + color2)/2);
                }
            }
        }
    }
}

void Reconstruct::getParameters(int projw, int projh, int camw, int camh, bool autocontrast, bool saveautocontrast, QString savePath)
{
    proj_w = projw;
    proj_h = projh;
    cameraWidth = camw;
    cameraHeight = camh;
    autoContrast_ = autocontrast;
    saveAutoContrast_ = saveautocontrast;
    savePath_ = savePath;//equal to projectPath
}
