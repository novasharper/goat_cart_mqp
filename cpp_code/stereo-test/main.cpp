/** Core **/
#include <iostream>
#include <string>
#include <chrono>
#include <map>

/** OpenCV **/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/calib3d.hpp>

/** Other **/
#include <yaml-cpp/yaml.h>
#include "defaults.h"
#include "settings.hpp"
#include "VidStream.hpp"

using namespace cv;
using Clock = std::chrono::steady_clock;
using std::chrono::time_point;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

#define P_base(imChannels, blockSize) (8 * imChannels * blockSize * blockSize)
#define CALC_DISP(width, scale) (((int) (width * scale / 8) + 15) & -16)

struct AppParams {
    double scaleFactor;
    int alg;
    int blockSize;
    int numDisparities;
    int disp12MaxDiff;
    int speckleRange;
    int speckleWindowSize;
    int preFilterCap;
    int uniquenessRatio;
};

void loadSettings(struct AppParams &s)
{
    YAML::Node p = Settings::get();
    YAML::Node m = p["matcher"];
    YAML::Node node;

    node = m["scaleFactor"];
    s.scaleFactor = node.IsScalar() ? node.as<float>() : DEFAULT_SCALE_FACTOR;

    node = m["alg"];
    s.alg = node.IsScalar() ? node.as<int>() : DEFAULT_ALG;

    if (s.alg == 1) s.blockSize = 3;
    else if (s.scaleFactor < 0.99) s.blockSize = 7;
    else s.blockSize = 15;

    int main_cam = m["main_cam"].as<int>();
    int default_width = p["video"]["sources"][main_cam]["res"][0].as<int>();
    s.numDisparities = CALC_DISP(default_width, s.scaleFactor);

    node = m["disp12MaxDiff"];
    s.disp12MaxDiff = node.IsScalar() ? node.as<int>() : DEFAULT_D12_MAX_DIFF;

    node = m["speckleRange"];
    s.speckleRange = node.IsScalar() ? node.as<int>() : DEFAULT_SPECKLE_RANGE;

    node = m["speckleWindowSize"];
    s.speckleWindowSize = node.IsScalar() ? node.as<int>() : DEFAULT_SPECKE_WS;

    node = m["preFilterCap"];
    s.preFilterCap = node.IsScalar() ? node.as<int>() : DEFAULT_PF_CAP;

    node = m["uniquenessRatio"];
    s.uniquenessRatio = node.IsScalar() ? node.as<int>() : DEFAULT_UNIQ_RATIO;
}

#define GETK(key, type) (t1[key] ? t1[key] : t2[key]).as<type>();

Ptr<StereoMatcher> initMatcher(struct AppParams *pSettings)
{
    Ptr<StereoBM> matcher = StereoBM::create(pSettings->numDisparities, pSettings->blockSize);
    //matcher->setDisp12MaxDiff(pSettings->disp12MaxDiff);
    //matcher->setSpeckleRange(pSettings->speckleRange);
    //matcher->setSpeckleWindowSize(pSettings->speckleWindowSize);
    matcher->setPreFilterCap(pSettings->preFilterCap);
    matcher->setUniquenessRatio(pSettings->uniquenessRatio);
    return matcher;
}

int main (int argc, char** argv )
{
    using namespace std::chrono_literals;
    //-
    std::cout << std::endl << "Loading params" << std::endl;
    YAML::Node cams_s = Settings::get()["video"]["sources"];
    struct AppParams pSettings;

    loadSettings(pSettings);

    // Open up cameras
    std::cout << std::endl << "Opening VideoCapture streams" << std::endl;
    std::map<std::string, VidStream> caps;
    for(int i = 0; i < cams_s.size(); i++)
    {
        caps[cams_s[i]["name"].as<std::string>()].open(cams_s[i],
            VidStream::filtResize + VidStream::filtGray);
    }

    std::cout << std::endl << "Creating StereoMatcher" << std::endl;
    Ptr<StereoMatcher> matcher_l = initMatcher(&pSettings);
    Ptr<ximgproc::DisparityWLSFilter> wls_filter = ximgproc::createDisparityWLSFilter(matcher_l);
    Ptr<StereoMatcher> matcher_r = ximgproc::createRightMatcher(matcher_l);
    wls_filter->setLambda(8000);
    wls_filter->setSigmaColor(1.0);

    std::cout << std::endl << "Entering main loop" << std::endl;

    ts_frame frame_l, frame_r;
    VidStream *cam_l = &caps["left"];
    VidStream *cam_r = &caps["right"];
    Mat disp_l, disp_r, disp_f, disp_vis;
    cam_l->start();
    cam_r->start();
    std::string tm_cap;
    double avg_time = 0;
#define DV disp_vis
    for (;;)
    {
        bool ready_l = cam_l->getFrame(frame_l);
        bool ready_r = cam_r->getFrame(frame_r);
        if (ready_l && ready_r)
        {
            time_point<Clock> start = Clock::now();
            matcher_l->compute(frame_l.frame, frame_r.frame, disp_l);
            matcher_r->compute(frame_r.frame, frame_l.frame, disp_r);
            wls_filter->filter(disp_l, frame_l.frame, disp_f, disp_r);
            ximgproc::getDisparityVis(disp_f, disp_vis, 1.75);
            time_point<Clock> end = Clock::now();
	    // Calc + disp filter time
            milliseconds diff = duration_cast<milliseconds>(end - start);
	    avg_time = avg_time * 0.75 + diff.count() * 0.25;
            tm_cap = std::to_string((int) (avg_time + 0.5));
            putText(DV, tm_cap, Point(10, 100), FONT_HERSHEY_PLAIN, 2.0, 	Scalar(255.0, 255.0, 255.0), 2);
            imshow("Disparity Map", DV);
        }
        if ((char) waitKey(50) == 'q') break;
    }
    cam_l->stop();
    cam_r->stop();

    return 0;
}
