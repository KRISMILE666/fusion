/*
 * @Author: blueclocker 1456055290@hnu.edu.cn
 * @Date: 2022-08-31 15:06:40
 * @LastEditors: blueclocker 1456055290@hnu.edu.cn
 * @LastEditTime: 2022-09-03 20:41:18
 * @FilePath: /wpollo/src/lanelet/path_boost/src/pathboost_core.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#include <cmath>
#include <ctime>
#include "path_boost/pathboost_core.h"
#include "path_boost/config/config_flags.hpp"
#include "glog/logging.h"
#include "path_boost/tools/time_helper.h"
#include "path_boost/reference_path_smoother/reference_path_smoother.hpp"
#include "path_boost/tools/tools.hpp"
#include "path_boost/data_struct/reference_path.hpp"
#include "path_boost/data_struct/basic_struct.hpp"
#include "path_boost/data_struct/vehicle_state_frenet.hpp"
#include "path_boost/tools/collosion_check.h"
#include "path_boost/tools/map.h"
#include "path_boost/tools/spline.h"
#include "path_boost/solver/base_solver.hpp"
#include "tinyspline_ros/tinysplinecpp.h"
#include "path_boost/reference_path_smoother/tension_smoother.hpp"


namespace PathBoostNS {

PathBoost::PathBoost(const BState &start_state,
                     const BState &end_state,
                     const grid_map::GridMap &map) :
    grid_map_(std::make_shared<Map>(map)),
    collision_checker_(std::make_shared<CollisionChecker>(map)),
    reference_path_(std::make_shared<ReferencePath>()),
    vehicle_state_(std::make_shared<VehicleState>(start_state, end_state, 0.0, 0.0)) {
}

bool PathBoost::solve(const std::vector<BState> &reference_points, std::vector<SLState> *final_path) {
    CHECK_NOTNULL(final_path);
    if (reference_points.empty()) {
        LOG(ERROR) << "Empty input, quit path optimization";
        return false;
    }

    TimeRecorder time_recorder("Outer Solve Function");
    time_recorder.recordTime("reference path smoothing");
    // Smooth reference path.
    // TODO: refactor this part!
    reference_path_->clear();
    auto reference_path_smoother = ReferencePathSmoother::create(FLAGS_smoothing_method,
                                                                 reference_points,
                                                                 vehicle_state_->getStartState(),
                                                                 *grid_map_);
    if (!reference_path_smoother->solve(reference_path_)) {
        LOG(ERROR) << "Path optimization FAILED!";
        return false;
    }

    time_recorder.recordTime("reference path segmentation");
    // Divide reference path into segments;
    if (!processReferencePath()) {
        LOG(ERROR) << "Path optimization FAILED!";
        return false;
    }

    time_recorder.recordTime("path optimization");
    // Optimize.
    if (!boostPath(final_path)) {
        LOG(ERROR) << "Path optimization FAILED!";
        return false;
    }
    time_recorder.recordTime("end");
    time_recorder.printTime();
    return true;
}

void PathBoost::processInitState() {
    // Calculate the initial deviation and the angle difference.
    BState init_point(reference_path_->getXS(0.0),
                     reference_path_->getYS(0.0),
                     getHeading(reference_path_->getXS(), reference_path_->getYS(), 0.0));
    auto first_point_local = global2Local(vehicle_state_->getStartState(), init_point);
    // In reference smoothing, the closest point to the vehicle is found and set as the
    // first point. So the distance here is simply the initial offset.
    double min_distance = distance(vehicle_state_->getStartState(), init_point);
    double initial_offset = first_point_local.y < 0.0 ? min_distance : -min_distance;
    double initial_heading_error = constrainAngle(vehicle_state_->getStartState().heading - init_point.heading);
    vehicle_state_->setInitError(initial_offset, initial_heading_error);
}

void PathBoost::setReferencePathLength() {
    BState end_ref_state(reference_path_->getXS(reference_path_->getLength()),
                        reference_path_->getYS(reference_path_->getLength()),
                        getHeading(reference_path_->getXS(), reference_path_->getYS(), reference_path_->getLength()));
    auto vehicle_target_to_end_ref_state = global2Local(end_ref_state, vehicle_state_->getTargetState());
    // Target state is out of ref line.
    if (vehicle_target_to_end_ref_state.x > 0.0) {
        return;
    }
    auto target_projection = getProjection(reference_path_->getXS(),
                                           reference_path_->getYS(),
                                           vehicle_state_->getTargetState().x,
                                           vehicle_state_->getTargetState().y,
                                           reference_path_->getLength(),
                                           0.0);
    reference_path_->setLength(target_projection.s);
}

bool PathBoost::processReferencePath() {
    if (isEqual(reference_path_->getLength(), 0.0)) {
        LOG(ERROR) << "Smoothed path is empty!";
        return false;
    }

    processInitState();
    // If the start heading differs a lot with the ref path, quit.
    if (fabs(vehicle_state_->getInitError().back()) > 75 * M_PI / 180) {
        LOG(ERROR) << "Initial psi error is larger than 75°, quit path optimization!";
        return false;
    }
    setReferencePathLength();

    reference_path_->buildReferenceFromSpline(FLAGS_output_spacing / 2.0, FLAGS_output_spacing);
    reference_path_->updateBounds(*grid_map_);
    return true;
}

bool PathBoost::boostPath(std::vector<SLState> *final_path) {
    CHECK_NOTNULL(final_path);
    final_path->clear();
    // TODO: remove arg: iter_num.
    std::vector<SLState> input_path;
    for (const auto &ref_state : reference_path_->getReferenceStates()) {
        SLState input_state;
        input_state.x = ref_state.x;
        input_state.y = ref_state.y;
        input_state.heading = ref_state.heading;
        input_state.s = ref_state.s;
        input_state.k = ref_state.k;
        input_path.push_back(input_state);
    }
    BaseSolver solver(*reference_path_, *vehicle_state_, input_path);
    TimeRecorder time_recorder("Optimize Path Function");
    // Solve with soft collision constraints.
    time_recorder.recordTime("Pre solving");
    if (!solver.solve(final_path)) {
        LOG(ERROR) << "Pre solving failed!";
        reference_path_->logBoundsInfo();
        return false;
    }
    time_recorder.recordTime("Update ref");
    // reference_path_->updateBoundsOnInputStates(*grid_map_, *input_path);
    // Solve.
    time_recorder.recordTime("Solving");
    // BaseSolver post_solver(*reference_path_, *vehicle_state_, *final_path);
    // if (!post_solver.solve(final_path)) {
    if (!solver.updateProblemFormulationAndSolve(*final_path, final_path)) {
        LOG(ERROR) << "Solving failed!";
        reference_path_->logBoundsInfo();
        return false;
    }
    time_recorder.recordTime("end");
    time_recorder.printTime();
    return true;
}

const ReferencePath &PathBoost::getReferencePath() const {
    return *reference_path_;
}
}



