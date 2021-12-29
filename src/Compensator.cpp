#ifndef __OBJECTS_H__
#define __OBJECTS_H__
#include "Headers/Common.hpp"
#include "Headers/Utils.hpp"
#include "Headers/Objects.hpp"
#include "Headers/Publishers.hpp"
#include "Headers/PointClouds.hpp"
#include "Headers/Accumulator.hpp"
#include "Headers/Compensator.hpp"
#include "Headers/Localizator.hpp"
#include "Headers/Mapper.hpp"
#endif

// class Compensator
    // public:
        PointCloud Compensator::compensate(double t1, double t2, bool global) {
            // Points from t1 to t2
            Points points = Accumulator::getInstance().get_points(t1, t2);
            if (points.empty()) return PointCloud();

            // (Integrated) States from t1 to t2
            States states = Accumulator::getInstance().get_states(t1, t2);
            IMUs imus = Accumulator::getInstance().get_imus(states.front().time, t2);
            States path_taken = this->integrate_imus(states, imus, t1, t2);

            if (t2 - t1 > 0.09) {
                // output.t1_t2(points, imus, states, t1, t2);
                // output.t1_t2(points, imus, path_taken, t1, t2);
                output.states(path_taken);

                // std::cout << "States:" << std::endl;
                // for (const State& s : states) std::cout << s.time << " " << s.pos.transpose() << " " << s.R.eulerAngles(2, 1, 0)(0) << std::endl;
                // std::cout << "Integrated:" << std::endl;
                // for (const State& s : path_taken) std::cout << s.time << " " << s.pos.transpose() << " " << s.R.eulerAngles(2, 1, 0)(0) << std::endl;
            }

            // Compensated pointcloud given a path
            return this->compensate(path_taken, points, true);
        }

        States Compensator::integrate_imus(double t1, double t2) {
            // (Integrated) States from t1 to t2
            States states = Accumulator::getInstance().get_states(t1, t2);
            IMUs imus = Accumulator::getInstance().get_imus(states.front().time, t2);
            return this->integrate_imus(states, imus, t1, t2);
        }

    // private:
        
        // TODO: Use already propagated states in case there's no LiDAR (more efficiency)
        States Compensator::integrate_imus(States& states, const IMUs& imus, double t1, double t2) {
            if (states.size() == 0) states.push_back(State(imus.back().time));
            int Ip = Accumulator::getInstance().before_first_state(imus, states);
            States integrated_states;

            // We add a ghost state at t2
            States states_amp = states;
            states_amp.push_back(State(t2));

            for (int Xp = 0; Xp < states.size(); ++Xp) {
                State integrated_state = states[Xp];
                State next_state = states_amp[Xp+1];
                bool last_state = Xp == states.size() - 1;

                // Integrate up until the next known state
                while (Ip < imus.size() and integrated_state.time < next_state.time) {
                    if (integrated_state.time >= t1)
                        integrated_states.push_back(integrated_state);
                    
                    if (last_state) integrated_state += imus[Ip++];
                    // TODO
                    // else integrated_state.interpolate(states, imus, Xp, Ip);
                    else integrated_state += imus[Ip++];
                }

                // Consider the case with t2-t1 < IMU sampling time
                if (last_state and integrated_states.size() == 0) {
                    integrated_states.push_back(integrated_state);
                }
            }
            
            return integrated_states;
        }

        PointCloud Compensator::compensate(States& states, Points& points, bool global) {
            // Get start and end states
            const State& Xt1 = states.front();
            const State& Xt2 = states.back();
            
            if (states.size() < 2) {
                ROS_ERROR("LOL states < 2");
                return PointCloud();
            }

            // Define state and point iterators
            int Xp = states.size() - 2;
            State Xtj = states[Xp];
            int Lp = points.size() - 1;

            // Compensate tj points to t2 frame
            PointCloud::Ptr result(new PointCloud());
            result->header.stamp = Conversions::sec2Microsec(Xt2.time);

            State XtLp = Xtj;
            State Xnow = Xtj;
            State Xnext = Xt2;

            while (Xt1.time <= Xnow.time) {
                
                // Find all points with time > Xnow.time 
                while (0 <= Lp and Xnow.time < points[Lp].time) {
                    // Integrate up to point time
                    XtLp += IMU (XtLp.a, XtLp.w, points[Lp].time);

                    // Get rotation-translation pairs
                    RotTransl t2_T_tj = XtLp - Xnext;
                    RotTransl I_T_L = XtLp.I_Rt_L();

                    // Transport point to Xnext
                    Point p_L_tj = points[Lp--];
                    Point t2_p_L_tj = I_T_L.inv() * t2_T_tj * I_T_L * p_L_tj;

                    // *result += p_L_tj;                         // Not compensated
                    if (not global) *result += t2_p_L_tj;         // LiDAR frame
                    else *result += Xnext * I_T_L * t2_p_L_tj;      // Global frame
                }

                Xnext = Xnow;

                // Depropagate state feeding IMUs in inverse order
                if (Xp > 0) Xnow = states[--Xp];
                else break;

                XtLp = Xnow;
            }

            return *result;
        }