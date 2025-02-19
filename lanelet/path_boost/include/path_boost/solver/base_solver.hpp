/*
 * @Author: blueclocker 1456055290@hnu.edu.cn
 * @Date: 2022-09-03 19:23:03
 * @LastEditors: blueclocker 1456055290@hnu.edu.cn
 * @LastEditTime: 2022-09-03 20:12:54
 * @FilePath: /wpollo/src/lanelet/path_boost/include/path_boost/solver/base_solver.hpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef PATH_OPTIMIZER_SOLVER_HPP
#define PATH_OPTIMIZER_SOLVER_HPP

#include <Eigen/Dense>
#include <memory>
#include <OsqpEigen/OsqpEigen.h>
#include "glog/logging.h"

namespace PathBoostNS {

class Config;
class ReferencePath;
class VehicleState;
class BState;
class SLState;

class BaseSolver {

 public:
    BaseSolver() = delete;

    BaseSolver(const ReferencePath &reference_path,
               const VehicleState &vehicle_state,
               const std::vector<SLState> &input_path);

    virtual ~BaseSolver() = default;

    static std::unique_ptr<BaseSolver> create(std::string &type,
                                              std::shared_ptr<ReferencePath> reference_path,
                                              std::shared_ptr<VehicleState> vehicle_state);

    virtual bool solve(std::vector<SLState> *optimized_path);

    virtual bool updateProblemFormulationAndSolve(const std::vector<SLState> &input_path, std::vector<SLState> *optimized_path);

 private:
    // Set Matrices for osqp solver.
    virtual void setCost(Eigen::SparseMatrix<double> *matrix_h) const;

    virtual void setConstraints(Eigen::SparseMatrix<double> *matrix_constraints,
                                Eigen::VectorXd *lower_bound,
                                Eigen::VectorXd *upper_bound) const;
    virtual void getOptimizedPath(const Eigen::VectorXd &optimization_result,
                                  std::vector<SLState> *optimized_path) const;
    std::pair<double, double> getSoftBounds(double lb, double ub, double safety_margin) const;
 protected:
    // Num of knots.
    const size_t n_{};
    size_t state_size_{};
    size_t control_size_{};
    size_t slack_size_{};
    size_t vars_size_{};
    size_t cons_size_{};
    size_t precise_planning_size_{};
    const ReferencePath &reference_path_;
    const VehicleState &vehicle_state_;
    std::vector<SLState> input_path_; // TODO: use pointer.
    OsqpEigen::Solver solver_;
    double reference_interval_;
    Eigen::SparseMatrix<double> linear_matrix_;
    Eigen::VectorXd lower_bound_;
    Eigen::VectorXd upper_bound_;
    Eigen::SparseMatrix<double> hessian_;
    Eigen::VectorXd gradient_;
};

}
#endif //PATH_OPTIMIZER_SOLVER_HPP
