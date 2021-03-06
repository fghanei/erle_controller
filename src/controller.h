#ifndef _CONTROLLER_H_
#define _CONTROLLER_H

#include "pid.h"
#include "mavros_msgs/OverrideRCIn.h"
#include "mavros_msgs/CommandBool.h"
#include "mavros_setstreamrate.h"

const bool CONTROL_ROLL=true;
const bool CONTROL_PITCH=true;
const bool CONTROL_YAW=false;
const bool CONTROL_THRUST=true;

//ROS node name
extern std::string NODE_NAME;

namespace drone {


/* A function to get parameter from node */
double get(
    const ros::NodeHandle& n,
    const std::string& name) {
    double value;
    n.getParam(name, value);
    ROS_WARN("Parameter %s: %.2f", name.c_str(), value);
    return value;
}

/***** Controller Class ******/
class Controller
{
private:
    enum State {
        Idle = 0,
        Armed = 1,
        TakingOff = 2,
        Automatic = 3,
        Landing_1 = 4,
        Landing_2 = 5,
    };

private:
    enum Channel {
        Roll = 0,
        Pitch = 1,
        Thrust = 2,
        Yaw = 3,
    };

public:
    /* Constructor with initialization */
    Controller(
        const std::string& worldFrame,
        const std::string& frame,
        const ros::NodeHandle& n)
        : m_worldFrame(worldFrame)
        , m_frame(frame)
        , m_listener()
        , m_pidX(
            get(n, NODE_NAME+"/PIDs/X/kp"),
            get(n, NODE_NAME+"/PIDs/X/kd"),
            get(n, NODE_NAME+"/PIDs/X/ki"),
            get(n, NODE_NAME+"/PIDs/X/minOutput"),
            get(n, NODE_NAME+"/PIDs/X/maxOutput"),
            get(n, NODE_NAME+"/PIDs/X/integratorMin"),
            get(n, NODE_NAME+"/PIDs/X/integratorMax"),
            get(n, NODE_NAME+"/PIDs/X/integratorApproximity"),
            "x")
        , m_pidY(
            get(n, NODE_NAME+"/PIDs/Y/kp"),
            get(n, NODE_NAME+"/PIDs/Y/kd"),
            get(n, NODE_NAME+"/PIDs/Y/ki"),
            get(n, NODE_NAME+"/PIDs/Y/minOutput"),
            get(n, NODE_NAME+"/PIDs/Y/maxOutput"),
            get(n, NODE_NAME+"/PIDs/Y/integratorMin"),
            get(n, NODE_NAME+"/PIDs/Y/integratorMax"),
            get(n, NODE_NAME+"/PIDs/Y/integratorApproximity"),
            "y")
        , m_pidZ(
            get(n, NODE_NAME+"/PIDs/Z/kp"),
            get(n, NODE_NAME+"/PIDs/Z/kd"),
            get(n, NODE_NAME+"/PIDs/Z/ki"),
            get(n, NODE_NAME+"/PIDs/Z/minOutput"),
            get(n, NODE_NAME+"/PIDs/Z/maxOutput"),
            get(n, NODE_NAME+"/PIDs/Z/integratorMin"),
            get(n, NODE_NAME+"/PIDs/Z/integratorMax"),
            get(n, NODE_NAME+"/PIDs/Z/integratorApproximity"),
            "z")
        , m_pidYaw(
            get(n, NODE_NAME+"/PIDs/Yaw/kp"),
            get(n, NODE_NAME+"/PIDs/Yaw/kd"),
            get(n, NODE_NAME+"/PIDs/Yaw/ki"),
            get(n, NODE_NAME+"/PIDs/Yaw/minOutput"),
            get(n, NODE_NAME+"/PIDs/Yaw/maxOutput"),
            get(n, NODE_NAME+"/PIDs/Yaw/integratorMin"),
            get(n, NODE_NAME+"/PIDs/Yaw/integratorMax"),
            get(n, NODE_NAME+"/PIDs/Yaw/integratorApproximity"),
            "yaw")
        , m_RC_roll_min(get(n, NODE_NAME+"/RC/roll/min"))
        , m_RC_roll_mid(get(n, NODE_NAME+"/RC/roll/mid"))
        , m_RC_roll_max(get(n, NODE_NAME+"/RC/roll/max"))
        , m_RC_pitch_min(get(n, NODE_NAME+"/RC/pitch/min"))
        , m_RC_pitch_mid(get(n, NODE_NAME+"/RC/pitch/mid"))
        , m_RC_pitch_max(get(n, NODE_NAME+"/RC/pitch/max"))
        , m_RC_yaw_min(get(n, NODE_NAME+"/RC/yaw/min"))
        , m_RC_yaw_mid(get(n, NODE_NAME+"/RC/yaw/mid"))
        , m_RC_yaw_max(get(n, NODE_NAME+"/RC/yaw/max"))
        , m_RC_thrust_min(get(n, NODE_NAME+"/RC/thrust/min"))
        , m_RC_thrust_mid(get(n, NODE_NAME+"/RC/thrust/mid"))
        , m_RC_thrust_max(get(n, NODE_NAME+"/RC/thrust/max"))
        , m_takeoff_thrustStep(get(n, NODE_NAME+"/Takeoff/thrustStep"))
        , m_takeoff_targetHeight(get(n, NODE_NAME+"/Takeoff/targetHeight"))
        , m_takeoff_liftThreshold(get(n, NODE_NAME+"/Takeoff/liftThreshold"))
        , m_landing_thrustStep(get(n, NODE_NAME+"/Landing/thrustStep"))
        , m_landing_targetHeight(get(n, NODE_NAME+"/Landing/targetHeight"))
        , m_landing_declineSpeed(get(n, NODE_NAME+"/Landing/declineSpeed"))
        , m_armThrust(get(n, NODE_NAME+"/ArmThrust"))
        , m_state(Idle)
        , m_pose_worldFrame()
        , m_goal_worldFrame()
        , m_goal_bodyFrame()
        , m_transform()
        , m_subscribeGoal()
        , m_serviceArm()
        , m_serviceDisarm()
        , m_serviceTakeoff()
        , m_serviceLand()
        , m_serviceStop()
        , m_serviceReleaseRC()
        , m_startZ(0)
    {
        ROS_INFO("controller object created");
        ros::NodeHandle nh;
        m_listener.waitForTransform(m_worldFrame, m_frame, ros::Time(0), ros::Duration(10.0)); 
        ROS_INFO("transform_listener result: %s",m_listener.allFramesAsString().c_str());
        m_pubRC = nh.advertise<mavros_msgs::OverrideRCIn>("mavros/rc/override",10);
        m_pubPose = nh.advertise<geometry_msgs::PoseStamped>("erle/pose",10);
        m_subscribeGoal = nh.subscribe("erle/goal", 2, &Controller::goalChanged, this);
        m_serviceTakeoff = nh.advertiseService("erle/takeoff", &Controller::takeoff, this);
        m_serviceLand = nh.advertiseService("erle/land", &Controller::land, this);
        m_serviceArm = nh.advertiseService("erle/arm", &Controller::arm, this);
        m_serviceDisarm = nh.advertiseService("erle/disarm", &Controller::disarm, this);
        m_serviceStop = nh.advertiseService("erle/stop", &Controller::stop, this);
        m_serviceReleaseRC = nh.advertiseService("erle/releaseRC", &Controller::releaseRC, this);
        ROS_INFO("Services advertised: erle/arm, disarm, takeoff, land, stop, releaseRC");
        ROS_WARN("*** Make sure you have set ThrustMid value correctly! ***");
        ROS_WARN("*** Make sure you have selected currect settings for Roll, Pitch, Yaw, Thrust control switches! ***");

        m_goal_worldFrame.pose.orientation.w = 1.0; //Just a value for default pose. Quaternions can't be all zero
    }

    void run(double frequency)
    {
        ros::NodeHandle node;
        ros::Timer timer = node.createTimer(ros::Duration(1.0/frequency), &Controller::iteration, this);
        while(ros::ok()) {
        ros::spinOnce();
        }
    }

private:
    /* applying PID values biased around middle stick values (hover value for thrust) */
    void rc_biasedOutput(geometry_msgs::Twist msg) {
        //ROS_INFO("[rc_biasedOutput]: PID_x(%.2f), PID_y(%.2f), PID_z(%.2f), PID_yaw(%.2f)", msg.linear.x, msg.linear.y, msg.linear.z, msg.angular.z);
        if (CONTROL_PITCH)    rc_setChannel(Pitch, m_RC_pitch_mid + msg.linear.x);
        else                  rc_setChannel(Pitch, 0);

        if (CONTROL_ROLL)     rc_setChannel(Roll, m_RC_roll_mid + msg.linear.y);
        else                  rc_setChannel(Roll, 0);

        if (CONTROL_YAW)      rc_setChannel(Yaw, m_RC_yaw_mid + msg.angular.z);
        else                  rc_setChannel(Yaw, 0);
      
        if (CONTROL_THRUST)   rc_setChannel(Thrust, m_RC_thrust_mid + msg.linear.z);
        else                  rc_setChannel(Thrust, 0);
    }

    void goalChanged(
        const geometry_msgs::PoseStamped::ConstPtr& msg)
    {
        ROS_INFO("New goal has been published in topic:\n\t\
            Poition       (x, y, z): (%.2f, %.2f, %.2f)\n\t\
            Orientation   (x, y, z, w): (%.2f, %.2f, %.2f, %.2f)",
            msg->pose.position.x, 
            msg->pose.position.y, 
            msg->pose.position.z, 
            msg->pose.orientation.x, 
            msg->pose.orientation.y, 
            msg->pose.orientation.z, 
            msg->pose.orientation.w);
        m_goal_worldFrame = *msg;
    }

    /* arm ErleCopter */
    bool arm(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res) {

        bool response;
        if (m_state==Idle) {
            //TODO need to verify/change flight mode
            ROS_INFO("Arm requested...");
            mavros_msgs::CommandBool armValue;
            armValue.request.value = true;
            if (ros::service::call("/mavros/cmd/arming", armValue)) {
                ROS_INFO("send armValue successful.");
                response = true;
                ROS_INFO("ThrustMin: %.2f, armThrust: %.2f", m_RC_thrust_min, m_armThrust);                
                rc_rollbackChannels();//initial value for thrust, we'll increase it gradually
                for (int i = m_RC_thrust_min; i < m_RC_thrust_min + m_armThrust; i++) {
                    rc_setChannel(Thrust, i);
                    rc_out();
                    ros::Duration(0.010).sleep();
                }
                m_state = Armed;
            } else {
                ROS_INFO("send armValue failed");
                response = false;
            }
        } else {
            ROS_INFO("state is not idle, cannot call arm.");
            response = false;
        }
        return response;
    }

    /* disarm ErleCopter */
    bool disarm(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res) {

        bool response;
        if (m_state == Armed || m_state == TakingOff || m_state == Automatic) {
            ROS_INFO("Disarm requested...");
            m_state = Idle;
            rc_rollbackChannels();
            ros::Duration(0.5).sleep();
            //put stick to disarm position
            rc_setChannel(Yaw, m_RC_yaw_min);
            rc_out();
            rc_out();
            ros::Duration(3.0).sleep();
            //TODO disarming through arming command does not work
            //mavros_msgs::CommandBool armValue;
            //armValue.request.value = false;
            //TODO disarming returns successful even if not disarmed. we have to get arm status.
            /*
            if (ros::service::call("/mavros/cmd/arming", armValue)) {
                ROS_INFO("send disarmValue successful.");
                response = true;
                rc_releaseChannels(0xFF);
                m_state = Idle;
            } else {
                ROS_INFO("send disarmValue failed");
                response = false;
            }
            */
            response = true;
            rc_releaseChannels(0xFF);
        } else {
            ROS_INFO("state is not armed, cannot call disarm.");
            response = false;
        }
        return response;
    }

    /* force release RC and go to Idle*/
    bool releaseRC(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res) {

        bool response;

        ROS_INFO("RC release requested!!!");

        m_state = Idle; //force going to idle;
        rc_releaseChannels(0xFF);
        response = true; //why not?

        return response;
    }

    /* stop and disarm ErleCopter giving control to RC, returns disarm response*/
    bool stop(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res) {

        bool response;

        ROS_INFO("Stop requested!!!");

        // force m_state to Armed, to call disarm
        m_state = Armed;
        std_srvs::Empty::Request dummy_req;
        std_srvs::Empty::Response dummy_res;
        disarm(dummy_req, dummy_res);
        
        m_state = Idle; //force going to idle;
        rc_releaseChannels(0xFF);
        response = true; //why not?

        return response;
    }

    /* Setting value for one of 4 RC channels, need to call rc_out afterwards */
    void rc_setChannel(Channel channel, int value) {
	    m_rc_override.channels[channel]=value;
    }

    /* Returning the value for one of 4 RC channels */
    int rc_getChannel(Channel channel) {
	    return m_rc_override.channels[channel];
    }

    /* publishing the RC values to override topic */    
    void rc_out(void) {
        m_pubRC.publish(m_rc_override);
        //ROS_INFO("rc_out: (%d,%d,%d,%d)", rc_getChannel(Roll), rc_getChannel(Pitch), rc_getChannel(Thrust), rc_getChannel(Yaw));
    }

    /* 
        all sticks in the middle, thrust to minimum.
        this is specified, in case of no RC, these will be last values.
    */          
    void rc_rollbackChannels(void) {
        ROS_INFO("rollback RC channels!");
        rc_setChannel(Roll, m_RC_roll_mid);
        rc_setChannel(Pitch, m_RC_pitch_mid);
        rc_setChannel(Yaw, m_RC_yaw_mid);
        rc_setChannel(Thrust, m_RC_thrust_min);
        rc_out();
    }

    /* release override of channels back to the RC, 
     use flags to indicate which channels, rest will be untouched */
    void rc_releaseChannels(int flags) {
        for (int i=0;i<8;i++) {
            if (flags & 1) {
                 if (m_state != Idle) {
                     ROS_INFO("Releasing channel %d ...",i);
                 }
                 m_rc_override.channels[i] = 0;
            }
            flags >> 1;
        }
        rc_out();
    }

    /* takeoff service */
    bool takeoff(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res) {

        bool response;
        if (m_state == Armed) {
            ROS_INFO("Takeoff requested...");
            
            m_goal_worldFrame.pose.position = m_pose_worldFrame.pose.position;
            m_goal_worldFrame.pose.orientation = m_pose_worldFrame.pose.orientation;
            m_goal_worldFrame.pose.position.z = m_takeoff_targetHeight;
            m_startZ = m_pose_worldFrame.pose.position.z;
            ROS_INFO("The original position is: (%.2f, %.2f, %.2f)", 
                       m_pose_worldFrame.pose.position.x, 
                       m_pose_worldFrame.pose.position.y, 
                       m_pose_worldFrame.pose.position.z);
            ROS_INFO("The goal for takeoff is : (%.2f, %.2f, %.2f)",
                       m_goal_worldFrame.pose.position.x, 
                       m_goal_worldFrame.pose.position.y, 
                       m_goal_worldFrame.pose.position.z);
            m_state = TakingOff;
            response = true;
        } else {
            ROS_INFO("state is not armed, cannot call takeoff.");
            response = false;
        }
        return response;
    }

    /* landing service */
    bool land(
        std_srvs::Empty::Request& req,
        std_srvs::Empty::Response& res)
    {

        bool response;
        if (m_state == TakingOff || m_state == Automatic) {
            ROS_INFO("Landing requested...");
            
            m_goal_worldFrame.pose.position = m_pose_worldFrame.pose.position;
            m_goal_worldFrame.pose.orientation = m_pose_worldFrame.pose.orientation;

            m_pidZ.disableIntegral();
            m_state = Landing_1;
            response = true;
        } else {
            ROS_INFO("state is not ready for landing!");
            response = false;
        }
        return response;
    }

    /* updating the local transform values */
    void updateTransform(void) {
        m_listener.lookupTransform(m_worldFrame, m_frame, ros::Time(0), m_transform);
        tf::Vector3 origin = m_transform.getOrigin();
        m_pose_worldFrame.pose.position.x = origin.x();
        m_pose_worldFrame.pose.position.y = origin.y();
        m_pose_worldFrame.pose.position.z = origin.z();
        tf::Quaternion orientation = m_transform.getRotation();
        m_pose_worldFrame.pose.orientation.x = orientation.x();
        m_pose_worldFrame.pose.orientation.y = orientation.y();
        m_pose_worldFrame.pose.orientation.z = orientation.z();
        m_pose_worldFrame.pose.orientation.w = orientation.w();

        m_goal_worldFrame.header.stamp = m_transform.stamp_;
        m_goal_worldFrame.header.frame_id = m_worldFrame;
        m_listener.transformPose(m_frame, m_goal_worldFrame, m_goal_bodyFrame);
//TODO 

        m_pubPose.publish(m_pose_worldFrame);
/*
        ROS_INFO("[updateTransform]: pose(%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f), goal_body(%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f)",
                 m_pose_worldFrame.pose.position.x,
                 m_pose_worldFrame.pose.position.y,
                 m_pose_worldFrame.pose.position.z,
                 m_pose_worldFrame.pose.orientation.x,
                 m_pose_worldFrame.pose.orientation.y, 
                 m_pose_worldFrame.pose.orientation.z,
                 m_pose_worldFrame.pose.orientation.w,
                 m_goal_bodyFrame.pose.position.x,
                 m_goal_bodyFrame.pose.position.y,
                 m_goal_bodyFrame.pose.position.z,
                 m_goal_bodyFrame.pose.orientation.x,
                 m_goal_bodyFrame.pose.orientation.y, 
                 m_goal_bodyFrame.pose.orientation.z,
                 m_goal_bodyFrame.pose.orientation.w
                );
*/
    }

    void pidReset()
    {
        m_pidX.reset();
        m_pidY.reset();
        m_pidZ.reset();
        m_pidYaw.reset();
    }

    /* applies PID values to RC channels based on target */
    void update_PID() {
        tfScalar roll, pitch, yaw;
        tf::Matrix3x3(
            tf::Quaternion(
                m_goal_bodyFrame.pose.orientation.x,
                m_goal_bodyFrame.pose.orientation.y,
                m_goal_bodyFrame.pose.orientation.z,
                m_goal_bodyFrame.pose.orientation.w
            )).getRPY(roll, pitch, yaw);
        
        geometry_msgs::Twist msg;
        msg.linear.x = m_pidX.update(0.0, m_goal_bodyFrame.pose.position.x);
        msg.linear.y = m_pidY.update(0.0, m_goal_bodyFrame.pose.position.y);
        msg.linear.z = m_pidZ.update(0.0, m_goal_bodyFrame.pose.position.z);
        //msg.angular.z = m_pidYaw.update(0.0, yaw);
/*
        if (iterationCounter==0) {

            ROS_INFO("Position      (x, y, z): (%.2f, %.2f, %.2f)",
            m_pose_worldFrame.pose.position.x, 
            m_pose_worldFrame.pose.position.y, 
            m_pose_worldFrame.pose.position.z); 
            ROS_INFO("Goal          (x, y, z): (%.2f, %.2f, %.2f)",
            m_goal_worldFrame.pose.position.x, 
            m_goal_worldFrame.pose.position.y, 
            m_goal_worldFrame.pose.position.z); 

            ROS_INFO("Relative Goal (x, y, z): (%.2f, %.2f, %.2f)",
            m_goal_bodyFrame.pose.position.x, 
            m_goal_bodyFrame.pose.position.y, 
            m_goal_bodyFrame.pose.position.z); 
            ROS_INFO("m_pidX(p, d, i)=(%.2f, %.2f, %.2f)", m_pidX.p(), m_pidX.d(), m_pidX.i());
            ROS_INFO("m_pidY(p, d, i)=(%.2f, %.2f, %.2f)", m_pidY.p(), m_pidY.d(), m_pidY.i());
            ROS_INFO("m_pidZ(p, d, i)=(%.2f, %.2f, %.2f)", m_pidZ.p(), m_pidZ.d(), m_pidZ.i());
        }
*/
        rc_biasedOutput(msg);
    }

    /* repeating each dt */
    void iteration(const ros::TimerEvent& e)
    {
        if (!ros::ok()) {
            // self call stop procedure
            std_srvs::Empty::Request dummy_req;
            std_srvs::Empty::Response dummy_res;
            stop(dummy_req, dummy_res);
            rc_releaseChannels(0xFF);
            ros::shutdown();
        }

        float dt = e.current_real.toSec() - e.last_real.toSec();

        updateTransform();

        switch(m_state) {
            case Idle:
            {
                //literally nothing to do
                return;
            }
            case Armed:
            {
                //rc_setChannel(Roll, m_RC_roll_mid);
                //rc_setChannel(Pitch, m_RC_pitch_mid);
                //prevent auto disarm
                if (rc_getChannel(Thrust) < m_RC_thrust_min + m_armThrust) {
                    //set the thrust smoothly
                    rc_setChannel(Thrust, rc_getChannel(Thrust) + m_takeoff_thrustStep * dt);
                } else {
                    //prevent overthrust
                    rc_setChannel(Thrust, m_RC_thrust_min + m_armThrust);
                }
                rc_out();
            }
            break;
            case TakingOff: 
            {
                if (rc_getChannel(Thrust) < m_RC_thrust_max) {
                    if (m_pose_worldFrame.pose.position.z < m_startZ + m_takeoff_liftThreshold) {
                        int currentThrust = rc_getChannel(Thrust);
                        //update_PID(); for now PID is off during takeoff. only thrust
                        rc_setChannel(Thrust, currentThrust +  m_takeoff_thrustStep * dt);
                    } else {
                        /* takeoff is over, shifting to automatic */
                        pidReset();
            		m_pidZ.enableIntegral();
                        ROS_INFO("Shifting to automatic mode..., current_z=%.2f", m_pose_worldFrame.pose.position.z);
                        m_state = Automatic;
                    }
                    rc_out();
                } else {
                    /* Full thrust but no lift? Any issue? */
                    ROS_INFO("Haven't lifted yet? Possible problems: no propeller, crash, etc.");
                    ROS_INFO("Changing state to Armed, then try to Disarm...");
                    std_srvs::Empty::Request dummy_req;
                    std_srvs::Empty::Response dummy_res;
                    m_state = Armed;
                    disarm(dummy_req, dummy_res);
                }
            }
            break;
            case Landing_1:
            {
                if (m_pose_worldFrame.pose.position.z > m_landing_targetHeight + m_startZ) {
                    //ROS_INFO("before.target height: %.2f, decline speed: %.2f, dt: %.2f",
                    //         m_goal_worldFrame.pose.position.z,  m_landing_declineSpeed, dt);
		    m_goal_worldFrame.pose.position.z -= m_landing_declineSpeed * dt;
                    //ROS_INFO("landing slowly, goal height is: %.2f", m_goal_worldFrame.pose.position.z);
                    update_PID();
                    rc_out();
                } else {
                    m_state = Landing_2;
                }
            }
            break;
            case Landing_2:
            {
                if (rc_getChannel(Thrust) > m_RC_thrust_min) {
                    rc_setChannel(Thrust, rc_getChannel(Thrust) - m_landing_thrustStep * dt);
                } else {
                    /* Landed? or Any issue? */
                    ROS_INFO("Landing procedure finished. Changing state to Armed");
                    ROS_INFO("Haven't landed yet? Possible problems: wrong height, crash, etc., call Disarm or Stop");
                    m_state = Armed;
                }
                rc_out();
            }
            break;
            case Automatic:
            {
                update_PID();
                rc_out();
            }
            break;
        }
        if (iterationCounter++ > 100) {

            ROS_INFO("The position is: (%.2f, %.2f, %.2f)", 
                       m_pose_worldFrame.pose.position.x, 
                       m_pose_worldFrame.pose.position.y, 
                       m_pose_worldFrame.pose.position.z);
            ROS_INFO("The goal is : (%.2f, %.2f, %.2f)",
                       m_goal_worldFrame.pose.position.x, 
                       m_goal_worldFrame.pose.position.y, 
                       m_goal_worldFrame.pose.position.z);
            ROS_INFO("Relative goal is : (%.2f, %.2f, %.2f)",
                       m_goal_bodyFrame.pose.position.x, 
                       m_goal_bodyFrame.pose.position.y, 
                       m_goal_bodyFrame.pose.position.z);
            ROS_INFO("RC_override: (%d, %d, %d, *)", rc_getChannel(Roll), rc_getChannel(Pitch), rc_getChannel(Thrust));
            iterationCounter=0;
        }
    }

private:
    std::string m_worldFrame;
    std::string m_frame;
    ros::Publisher m_pubRC;
    ros::Publisher m_pubPose;
    tf::TransformListener m_listener;
    PID m_pidX;
    PID m_pidY;
    PID m_pidZ;
    PID m_pidYaw;
    State m_state;
    geometry_msgs::PoseStamped m_pose_worldFrame;
    geometry_msgs::PoseStamped m_goal_worldFrame;
    geometry_msgs::PoseStamped m_goal_bodyFrame;
    tf::StampedTransform m_transform;
    mavros_msgs::OverrideRCIn m_rc_override;
    ros::Subscriber m_subscribeGoal;
    ros::ServiceServer m_serviceTakeoff;
    ros::ServiceServer m_serviceLand;
    ros::ServiceServer m_serviceArm;
    ros::ServiceServer m_serviceDisarm;
    ros::ServiceServer m_serviceStop;
    ros::ServiceServer m_serviceReleaseRC;
    float m_startZ;
    int iterationCounter;
    float m_RC_roll_min;
    float m_RC_roll_mid;
    float m_RC_roll_max;
    float m_RC_pitch_min;
    float m_RC_pitch_mid;
    float m_RC_pitch_max;
    float m_RC_yaw_min;
    float m_RC_yaw_mid;
    float m_RC_yaw_max;
    float m_RC_thrust_min;
    float m_RC_thrust_mid;
    float m_RC_thrust_max;
    float m_takeoff_thrustStep;
    float m_takeoff_targetHeight;
    float m_takeoff_liftThreshold;
    float m_landing_thrustStep;
    float m_landing_targetHeight;
    float m_landing_declineSpeed;
    float m_armThrust;

};

}

#endif /* _CONTROLLER_H */
