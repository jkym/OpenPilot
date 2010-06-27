/**
 * \file demo_slam.cpp
 *
 * ## Add brief description here ##
 *
 * \author jsola@laas.fr
 * \date 28/04/2010
 *
 *  ## Add a description here ##
 *
 * \ingroup rtslam
 */

#include <iostream>
#include <boost/shared_ptr.hpp>
#include <time.h>
#include <map>

// jafar debug include
#include "kernel/jafarDebug.hpp"
#include "kernel/jafarTestMacro.hpp"
#include "kernel/timingTools.hpp"
#include "jmath/random.hpp"
#include "jmath/matlab.hpp"

#include "correl/explorer.hpp"
#include "rtslam/quickHarrisDetector.hpp"

#include "jmath/ublasExtra.hpp"

#include "rtslam/rtSlam.hpp"
//#include "rtslam/robotOdometry.hpp"
#include "rtslam/robotConstantVelocity.hpp"
//#include "rtslam/robotInertial.hpp"
#include "rtslam/sensorPinHole.hpp"
#include "rtslam/landmarkAnchoredHomogeneousPoint.hpp"
//#include "rtslam/landmarkEuclideanPoint.hpp"
#include "rtslam/observationFactory.hpp"
#include "rtslam/observationMakers.hpp"
#include "rtslam/activeSearch.hpp"
#include "rtslam/featureAbstract.hpp"
#include "rtslam/rawImage.hpp"
#include "rtslam/descriptorImagePoint.hpp"
#include "rtslam/dataManagerActiveSearch.hpp"

#include "rtslam/hardwareSensorCameraFirewire.hpp"
//#include "rtslam/hardwareEstimatorMti.hpp"

#include "rtslam/display_qt.hpp"

using namespace jblas;
using namespace jafar;
using namespace jafar::jmath;
using namespace jafar::jmath::ublasExtra;
using namespace jafar::rtslam;
using namespace boost;

typedef ImagePointObservationMaker<ObservationPinHoleEuclideanPoint, SensorPinHole, LandmarkEuclideanPoint,
    SensorAbstract::PINHOLE, LandmarkAbstract::PNT_EUC> PinholeEucpObservationMaker;
typedef ImagePointObservationMaker<ObservationPinHoleAnchoredHomogeneousPoint, SensorPinHole,
    LandmarkAnchoredHomogeneousPoint, SensorAbstract::PINHOLE, LandmarkAbstract::PNT_AH> PinholeAhpObservationMaker;

int mode = 0;
std::string dump_path = ".";

void demo_slam01_main(world_ptr_t *world) {

	const int MAP_SIZE = 250;

	const int N_FRAMES = 5000;

	const double UNCERT_VLIN = 0.05; // m/s
	const double UNCERT_VANG = 0.05; // rad/s
	const double PERT_VLIN = 1.0; // m/s per sqrt(s)
	const double PERT_VANG = 1.0; // rad/s per sqrt(s)

	const double FRAME_RATE = 60;

	const int IMG_WIDTH = 640;
	const int IMG_HEIGHT = 480;
	const double PIX_NOISE = 1.0;
	const double D_MIN = 0.1;

	const int GRID_VCELLS = 4;
	const int GRID_HCELLS = 4;
	const int GRID_MARGIN = 10;
	const int GRID_SEPAR = 10;

	const int PATCH_SIZE = 11;
	const double MATCH_TH = 0.90;
	const double SEARCH_SIGMA = 3.0;

	const int HARRIS_CONV_SIZE = 5;
	const double HARRIS_TH = 15.0;
	const double HARRIS_EDDGE = 2.0;
	const int PATCH_DESC = 3*PATCH_SIZE;

//	const bool SHOW_PATCH = true;

	time_t rseed = time(NULL);
	if (mode == 1) {
		std::fstream f((dump_path + std::string("/rseed.log")).c_str(), std::ios_base::out);
		f << rseed << std::endl;
		f.close();
	}
	else if (mode == 2) {
		std::fstream f((dump_path + std::string("/rseed.log")).c_str(), std::ios_base::in);
		f >> rseed;
		f.close();
	}
	std::cout << "rseed " << rseed << std::endl;
	srand(rseed);


	double _d[2] = { -0.27572, 0.28827 }; //{-0.27965, 0.20059, -0.14215}; //{-0.27572, 0.28827};
	//	double _d[0];
	vec d = createVector<2> (_d);
	double _k[4] = { 301.60376, 266.29546, 519.67406, 519.54656 };
	//	double _k[4] = {320, 240, 320, 320};
	vec k = createVector<4> (_k);

	boost::shared_ptr<ObservationFactory> obsFact(new ObservationFactory());
	obsFact->addMaker(boost::shared_ptr<ObservationMakerAbstract>(new PinholeEucpObservationMaker(PATCH_SIZE, D_MIN)));
	obsFact->addMaker(boost::shared_ptr<ObservationMakerAbstract>(new PinholeAhpObservationMaker(PATCH_SIZE, D_MIN)));


	// ---------------------------------------------------------------------------
	// --- INIT ------------------------------------------------------------------
	// ---------------------------------------------------------------------------
	// INIT : 1 map and map-manager, 2 robs, 3 sens and data-manager.
	world_ptr_t worldPtr = *world;
	worldPtr->display_mutex.lock();


	// 1. Create maps.
	map_ptr_t mapPtr(new MapAbstract(MAP_SIZE));
	worldPtr->addMap(mapPtr);
	mapPtr->clear();
	// 1b. Create map manager.
	boost::shared_ptr<MapManager<LandmarkAnchoredHomogeneousPoint, LandmarkEuclideanPoint> > mmPoint(new MapManager<
	    LandmarkAnchoredHomogeneousPoint, LandmarkEuclideanPoint> ());
	mmPoint->linkToParentMap(mapPtr);


	// 2. Create robots.
	robconstvel_ptr_t robPtr1(new RobotConstantVelocity(mapPtr));
	robPtr1->setId();
	robPtr1->linkToParentMap(mapPtr);
	robPtr1->state.clear();
	robPtr1->pose.x(quaternion::originFrame());
	robPtr1->dt_or_dx = 1. / FRAME_RATE;
	double _v[6] = { PERT_VLIN, PERT_VLIN, PERT_VLIN, PERT_VANG, PERT_VANG, PERT_VANG };
	robPtr1->perturbation.clear();
	robPtr1->perturbation.set_std_continuous(createVector<6> (_v));
	robPtr1->setVelocityStd(UNCERT_VLIN,UNCERT_VANG);
	robPtr1->constantPerturbation = false;

	// 3. Create sensors.
	pinhole_ptr_t senPtr11(new SensorPinHole(robPtr1, MapObject::UNFILTERED));
	senPtr11->setId();
	senPtr11->linkToParentRobot(robPtr1);
	senPtr11->state.clear();
	senPtr11->pose.x(quaternion::originFrame());
	senPtr11->params.setImgSize(IMG_WIDTH, IMG_HEIGHT);
	senPtr11->params.setIntrinsicCalibration(k, d, d.size());
	senPtr11->params.setMiscellaneous(1.0, 0.1);
	senPtr11->params.patchSize = -1; // FIXME: See how to propagate patch size properly (obs factory needs it to be in sensor)

	// 3b. Create data manager.
	boost::shared_ptr<ActiveSearchGrid> asGrid(new ActiveSearchGrid(IMG_WIDTH, IMG_HEIGHT, GRID_HCELLS, GRID_VCELLS, GRID_MARGIN, GRID_SEPAR));
	boost::shared_ptr<QuickHarrisDetector> harrisDetector(new QuickHarrisDetector(HARRIS_CONV_SIZE, HARRIS_TH, HARRIS_EDDGE));
	boost::shared_ptr<correl::Explorer<correl::Zncc> > znccMatcher(new correl::Explorer<correl::Zncc>());
	boost::shared_ptr<DataManagerActiveSearch<RawImage, SensorPinHole, QuickHarrisDetector, correl::Explorer<correl::Zncc> > > dmPt11(new DataManagerActiveSearch<RawImage,
			SensorPinHole, QuickHarrisDetector, correl::Explorer<correl::Zncc> > ());
	dmPt11->linkToParentSensorSpec(senPtr11);
	dmPt11->linkToParentMapManager(mmPoint);
	dmPt11->setActiveSearchGrid(asGrid);
	dmPt11->setDetector(harrisDetector, PATCH_DESC, PIX_NOISE);
	dmPt11->setMatcher(znccMatcher, PATCH_SIZE, SEARCH_SIGMA, MATCH_TH, PIX_NOISE);
	dmPt11->setObservationFactory(obsFact);

	viam_hwmode_t hwmode = { VIAM_HWSZ_640x480, VIAM_HWFMT_MONO8, VIAM_HW_FIXED, VIAM_HWFPS_60, VIAM_HWTRIGGER_INTERNAL };
//	 UNCOMMENT THESE TWO LINES TO ENABLE FIREWIRE CAMERA OPERATION
	hardware::hardware_sensor_ptr_t hardSen11(new hardware::HardwareSensorCameraFirewire("0x00b09d01006fb38f", hwmode, mode, dump_path));
	senPtr11->setHardwareSensor(hardSen11);

	// Show empty map
	cout << *mapPtr << endl;

	worldPtr->display_mutex.unlock();


	// ---------------------------------------------------------------------------
	// --- LOOP ------------------------------------------------------------------
	// ---------------------------------------------------------------------------
	// INIT : complete observations set
	// loop all sensors
	// loop all lmks
	// create sen--lmk observation
	// Temporal loop

	kernel::Chrono chrono;
	kernel::Chrono total_chrono;
	kernel::Chrono mutex_chrono;
	double max_dt = 0;
	for (int t = 1; t <= N_FRAMES;) {
		bool had_data = false;

			worldPtr->display_mutex.lock();
			// cout << "\n************************************************** " << endl;
			// cout << "\n                 FRAME : " << t << " (blocked "
			//      << mutex_chrono.elapsedMicrosecond() << " us)" << endl;
			chrono.reset();
			int total_match_time = 0;
			int total_update_time = 0;


			// foreach robot
			for (MapAbstract::RobotList::iterator robIter = mapPtr->robotList().begin(); robIter != mapPtr->robotList().end(); robIter++) {
				robot_ptr_t robPtr = *robIter;


				// cout << "\n================================================== " << endl;
				// cout << *robPtr << endl;

				// foreach sensor
				for (RobotAbstract::SensorList::iterator senIter = robPtr->sensorList().begin(); senIter
				    != robPtr->sensorList().end(); senIter++) {
					sensor_ptr_t senPtr = *senIter;
					//					cout << "\n________________________________________________ " << endl;
					//					cout << *senPtr << endl;


					// get raw-data
					if (senPtr->acquireRaw() < 0)
						continue;
					else had_data=true;
					//std::cout << chronototal.elapsed() << " has acquired" << std::endl;
cout << "\nNEW FRAME\n" << endl;
					// move the filter time to the data raw.
					vec u(robPtr->mySize_control()); // TODO put some real values in u.
					fillVector(u, 0.0);
					robPtr->move(u, senPtr->getRaw()->timestamp);

					cout << *robPtr << endl;

					// foreach dataManager
					for (SensorAbstract::DataManagerList::iterator dmaIter = senPtr->dataManagerList().begin(); dmaIter
					    != senPtr->dataManagerList().end(); dmaIter++) {
						data_manager_ptr_t dmaPtr = *dmaIter;
						dmaPtr->process(senPtr->getRaw());
					} // foreach dataManager

			} // for each sensor
		} // for each robot


		if (had_data) {
			t++;

			if (robPtr1->dt_or_dx > max_dt) max_dt = robPtr1->dt_or_dx;

			// Output info
			cout << endl;
			cout << "dt: " << (int) (1000 * robPtr1->dt_or_dx) << "ms (match "
			<< total_match_time/1000 << " ms, update " << total_update_time/1000 << "ms). Lmk: [";
			cout << mmPoint->landmarkList().size() << "] ";
			for (MapManagerAbstract::LandmarkList::iterator lmkIter =
					mmPoint->landmarkList().begin(); lmkIter
					!= mmPoint->landmarkList().end(); lmkIter++) {
				cout << (*lmkIter)->id() << " ";
			}
			// TODO-NMSD: MM::reparam+delete

		} // if had_data

		worldPtr->display_mutex.unlock();
		mutex_chrono.reset();

	} // temporal loop

	cout << "time avg(max): " << total_chrono.elapsed()/N_FRAMES << "(" << (int)(1000*max_dt) <<") ms" << endl;
	std::cout << "\nFINISHED !" << std::endl;

	sleep(60);
} // demo_slam01_main


void demo_slam01_display(world_ptr_t *world) {
	//(*world)->display_mutex.lock();
	qdisplay::qtMutexLock<kernel::FifoMutex>((*world)->display_mutex);
	display::ViewerQt *viewerQt = static_cast<display::ViewerQt*> ((*world)->getDisplayViewer(display::ViewerQt::id()));
	if (viewerQt == NULL) {
		viewerQt = new display::ViewerQt();
		(*world)->addDisplayViewer(viewerQt, display::ViewerQt::id());
	}
	viewerQt->bufferize(*world);
	(*world)->display_mutex.unlock();

	viewerQt->render();
}

	void demo_slam01(bool display) {
		world_ptr_t worldPtr(new WorldAbstract());

		// to start with qt display
		const int slam_priority = -20; // needs to be started as root to be < 0
		const int display_priority = 10;
		const int display_period = 100; // ms
		if (display)
		{
			qdisplay::QtAppStart((qdisplay::FUNC)&demo_slam01_display,display_priority,(qdisplay::FUNC)&demo_slam01_main,slam_priority,display_period,&worldPtr);
		}
		else
		{
			kernel::setCurrentThreadPriority(slam_priority);
			demo_slam01_main(&worldPtr);
		}

		JFR_DEBUG("Terminated");
	}

		/**
		 * Function call usage:
		 *
		 * 	demo_slam DISP DUMP PATH
		 *
		 * If you want display, pass a first argument DISP="1" to the executable, otherwise "0".
		 * If you want to dump images, pass a second argument to the executable DUMP="1" and a path where
		 * you want the processed images be saved. If you want to replay the last execution, change 1 to 2
		 *
		 * Example 1: demo_slam 1 1 /home/you/rtslam dumps images to /home/you/rtslam
		 * example 2: demo_slam 1 2 /home/you/rtslam replays the saved sequence
		 * example 3: demo_slam 1 0 /anything does not dump
		 * example 4: demo_slam 0 any /anything does not display nor dump.
		 */
		int main(int argc, const char* argv[])
		{
			bool display = 1;
			if (argc == 4)
			{
				display = atoi(argv[1]);
				mode = atoi(argv[2]);
				dump_path = argv[3];
			}
			else if (argc != 0)
			std::cout << "Usage: demo_slam <display-enabled=1> <image-mode=0> <dump-path=.>" << std::endl;

			demo_slam01(display);
		}
