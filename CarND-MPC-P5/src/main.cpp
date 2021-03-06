#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
      // "42" at the start of the message means there's a websocket message event.
      // The 4 signifies a websocket message
      // The 2 signifies a websocket event
      string sdata = string(data).substr(0, length);
      cout << sdata << endl;
      if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
        string s = hasData(sdata);
        if (s != "") {
          auto j = json::parse(s);
          string event = j[0].get<string>();
          if (event == "telemetry") {
            // j[1] is the data JSON object
            vector<double> ptsx = j[1]["ptsx"];
            vector<double> ptsy = j[1]["ptsy"];
            double px = j[1]["x"];
            double py = j[1]["y"];
            double psi = j[1]["psi"];
            double v = j[1]["speed"];
            const double delta = j[1]["steering_angle"];
            const double a = j[1]["throttle"];
            /*
            * Calculate steeering angle and throttle using MPC.
            *
            * Both are in between [-1, 1].
            *
            */

            vector<double> vehicle_xs(ptsx.size());
            vector<double> vehicle_ys(ptsy.size());
            for (auto i = 0; i < ptsx.size(); ++i) {
              // centerize
              double centered_x = ptsx[i] - px;
              double centered_y = ptsy[i] - py;
              // rotate
              vehicle_xs[i] = centered_x * cos(-psi) - centered_y * sin(-psi);
              vehicle_ys[i] = centered_x * sin(-psi) + centered_y * cos(-psi);
            }

            Eigen::Map<Eigen::VectorXd> xs(&vehicle_xs[0], vehicle_xs.size());
            Eigen::Map<Eigen::VectorXd> ys(&vehicle_ys[0], vehicle_ys.size());
            auto coeffs = polyfit(xs, ys, 3);
              size_t N = 15;
              std::vector<double> next_xs(N);
              std::vector<double> next_ys(N);
              const double D = 3.0;
              for (int i = 0; i < N; i++) {
                  const double dx = D * i;
                  const double dy = coeffs[3] * dx * dx * dx + coeffs[2] * dx * dx + coeffs[1] * dx + coeffs[0];
                  next_xs[i] = dx;
                  next_ys[i] = dy;
              }

            double cte = polyeval(coeffs, 0) /*- py*/;
            double epsi = atan(coeffs[1]) /*- psi*/;
//
//            Eigen::VectorXd state(6);
//            state << 0,0,0, v, cte, epsi;

            const double dt = 0.1;
            const double Lf = 2.67;
            const double current_px = 0.0 + v * dt;
            const double current_py = 0.0;
            const double current_psi = 0.0 + v * (-delta) / Lf * dt;
            const double current_v = v + a * dt;
            const double current_cte = cte + v * sin(epsi) * dt;
            const double current_epsi = epsi + v * (-delta) / Lf * dt;

            const int NUMBER_OF_STATES = 6;
            Eigen::VectorXd state(NUMBER_OF_STATES);
            state << current_px, current_py, current_psi, current_v, current_cte, current_epsi;

            auto actuator = mpc.Solve(state, coeffs);
            double steer_value = -actuator[0];
            double throttle_value = actuator[1];

            json msgJson;
            // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
            // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
            msgJson["steering_angle"] = steer_value;
            msgJson["throttle"] = throttle_value;

            //Display the MPC predicted trajectory

            //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
            // the points in the simulator are connected by a Green line

            msgJson["mpc_x"] = next_xs;
            msgJson["mpc_y"] = next_ys;

            //Display the waypoints/reference line

            //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
            // the points in the simulator are connected by a Yellow line

            msgJson["next_x"] = mpc.future_xs;
            msgJson["next_y"] = mpc.future_ys;


            auto msg = "42[\"steer\"," + msgJson.dump() + "]";
            std::cout << msg << std::endl;
            // Latency
            // The purpose is to mimic real driving conditions where
            // the car does actuate the commands instantly.
            //
            // Feel free to play around with this value but should be to drive
            // around the track with 100ms latency.
            //
            // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
            // SUBMITTING.
            this_thread::sleep_for(chrono::milliseconds(100));
            ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          }
        } else {
          // Manual driving
          std::string msg = "42[\"manual\",{}]";
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
      const std::string s = "<h1>Hello world!</h1>";
      if (req.getUrl().valueLength == 1) {
        res->end(s.data(), s.length());
      } else {
        // i guess this should be done more gracefully?
        res->end(nullptr, 0);
      }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
      std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
      ws.close();
      std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
