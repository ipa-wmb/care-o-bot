/*!
 *****************************************************************
 * \file
 *
 * \note
 *   Copyright (c) 2010 \n
 *   Fraunhofer Institute for Manufacturing Engineering
 *   and Automation (IPA) \n\n
 *
 *****************************************************************
 *
 * \note
 *   Project name: care-o-bot
 * \note
 *   ROS stack name: cob_driver
 * \note
 *   ROS package name: cob_powercube_chain
 *
 * \author
 *   Author: Florian Weisshardt, email:florian.weisshardt@ipa.fhg.de
 * \author
 *   Supervised by: Florian Weisshardt, email:florian.weisshardt@ipa.fhg.de
 *
 * \date Date of creation: Jan 2010
 *
 * \brief
 *   Implementation of ROS node for powercube_chain.
 *
 *****************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     - Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer. \n
 *     - Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution. \n
 *     - Neither the name of the Fraunhofer Institute for Manufacturing
 *       Engineering and Automation (IPA) nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission. \n
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License LGPL as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License LGPL for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License LGPL along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

//##################
//#### includes ####

// standard includes
//--

// ROS includes
#include <ros/ros.h>
#include <urdf/model.h>
#include <actionlib/server/simple_action_server.h>
#include <pr2_controllers_msgs/JointTrajectoryAction.h>
#include <pr2_controllers_msgs/JointTrajectoryControllerState.h>

// ROS message includes
#include <sensor_msgs/JointState.h>
#include <trajectory_msgs/JointTrajectory.h>

// ROS service includes
#include <cob_srvs/Trigger.h>
#include <cob_srvs/SetOperationMode.h>

// external includes
#include <cob_powercube_chain/PowerCubeCtrl.h>
#include <cob_powercube_chain/simulatedArm.h>

/*!
* \brief Implementation of ROS node for powercube_chain.
*
* Offers actionlib and direct command interface.
*/
class PowercubeChainNode
{
	//
	public:
		/// create a handle for this node, initialize node
		ros::NodeHandle n_;

		// declaration of topics to publish
		ros::Publisher topicPub_JointState_;
		ros::Publisher topicPub_ControllerState_;

		// declaration of topics to subscribe, callback is called for new messages arriving
		ros::Subscriber topicSub_DirectCommand_;

		// declaration of service servers
		ros::ServiceServer srvServer_Init_;
		ros::ServiceServer srvServer_Stop_;
		ros::ServiceServer srvServer_Recover_;
		ros::ServiceServer srvServer_SetOperationMode_;

		// actionlib server
		actionlib::SimpleActionServer<pr2_controllers_msgs::JointTrajectoryAction> as_;
		std::string action_name_;
		// create messages that are used to published feedback/result
		pr2_controllers_msgs::JointTrajectoryFeedback feedback_;
		pr2_controllers_msgs::JointTrajectoryResult result_;

		// declaration of service clients
		//--

		// member variables
#ifndef SIMU
		PowerCubeCtrl* PCube_;
#else
		simulatedArm* PCube_;
#endif
		PowerCubeCtrlParams* PCubeParams_;
		std::string CanModule_;
		int CanDevice_;
		int CanBaudrate_;
		XmlRpc::XmlRpcValue ModIds_param_;
		std::vector<int> ModIds_;
		XmlRpc::XmlRpcValue JointNames_param_;
		std::vector<std::string> JointNames_;
		XmlRpc::XmlRpcValue MaxAcc_param_;
		std::vector<double> MaxAcc_;
		bool isInitialized_;
		bool finished_;
		std::vector<double>cmd_vel_;
		bool newvel_;
		
		trajectory_msgs::JointTrajectory traj_;
		trajectory_msgs::JointTrajectoryPoint traj_point_;
		int traj_point_nr_;

		/*!
		* \brief Constructor for PowercubeChainNode class.
		*
		* \param name Name for the actionlib server.
		*/
		PowercubeChainNode(std::string name):
			as_(n_, name, boost::bind(&PowercubeChainNode::executeCB, this, _1)),
			action_name_(name)
		{
			isInitialized_ = false;
			traj_point_nr_ = 0;
			finished_ = false;

#ifndef SIMU
			PCube_ = new PowerCubeCtrl();
#else
			PCube_ = new simulatedArm();
#endif
			PCubeParams_ = new PowerCubeCtrlParams();

			// implementation of topics to publish
			topicPub_JointState_ = n_.advertise<sensor_msgs::JointState>("/joint_states", 1);
			topicPub_ControllerState_ = n_.advertise<pr2_controllers_msgs::JointTrajectoryControllerState>("controller_state", 1);

			// implementation of topics to subscribe
			topicSub_DirectCommand_ = n_.subscribe("command", 1, &PowercubeChainNode::topicCallback_DirectCommand, this);

			// implementation of service servers
			srvServer_Init_ = n_.advertiseService("init", &PowercubeChainNode::srvCallback_Init, this);
			srvServer_Stop_ = n_.advertiseService("stop", &PowercubeChainNode::srvCallback_Stop, this);
			srvServer_Recover_ = n_.advertiseService("recover", &PowercubeChainNode::srvCallback_Recover, this);
			srvServer_SetOperationMode_ = n_.advertiseService("set_operation_mode", &PowercubeChainNode::srvCallback_SetOperationMode, this);

			// implementation of service clients
			//--

			// read parameters from parameter server
			n_.getParam("CanModule", CanModule_);
			n_.getParam("CanDevice", CanDevice_);
			n_.getParam("CanBaudrate", CanBaudrate_);
			ROS_INFO("CanModule=%s, CanDevice=%d, CanBaudrate=%d",CanModule_.c_str(),CanDevice_,CanBaudrate_);

			// get ModIds from parameter server
			if (n_.hasParam("ModIds"))
			{
				n_.getParam("ModIds", ModIds_param_);
			}
			else
			{
				ROS_ERROR("Parameter ModIds not set");
			}
			ModIds_.resize(ModIds_param_.size());
			for (int i = 0; i<ModIds_param_.size(); i++ )
			{
				ModIds_[i] = (int)ModIds_param_[i];
			}
			std::cout << "ModIds = " << ModIds_param_ << std::endl;
			
			// get JointNames from parameter server
			ROS_INFO("getting JointNames from parameter server");
			if (n_.hasParam("JointNames"))
			{
				n_.getParam("JointNames", JointNames_param_);
			}
			else
			{
				ROS_ERROR("Parameter JointNames not set");
			}
			JointNames_.resize(JointNames_param_.size());
			for (int i = 0; i<JointNames_param_.size(); i++ )
			{
				JointNames_[i] = (std::string)JointNames_param_[i];
			}
			std::cout << "JointNames = " << JointNames_param_ << std::endl;

			PCubeParams_->Init(CanModule_, CanDevice_, CanBaudrate_, ModIds_);
			
			// get MaxAcc from parameter server
			ROS_INFO("getting MaxAcc from parameter server");
			if (n_.hasParam("MaxAcc"))
			{
				n_.getParam("MaxAcc", MaxAcc_param_);
			}
			else
			{
				ROS_ERROR("Parameter MaxAcc not set");
			}
			MaxAcc_.resize(MaxAcc_param_.size());
			for (int i = 0; i<MaxAcc_param_.size(); i++ )
			{
				MaxAcc_[i] = (double)MaxAcc_param_[i];
			}
			PCubeParams_->SetMaxAcc(MaxAcc_);
			std::cout << "MaxAcc = " << MaxAcc_param_ << std::endl;
			
			// load parameter server string for robot/model
			std::string param_name = "robot_description";
			std::string full_param_name;
			std::string xml_string;
			n_.searchParam(param_name,full_param_name);
			n_.getParam(full_param_name.c_str(),xml_string);
			ROS_INFO("full_param_name=%s",full_param_name.c_str());
			if (xml_string.size()==0)
			{
				ROS_ERROR("Unable to load robot model from param server robot_description\n");
				exit(2);
			}
			ROS_DEBUG("%s content\n%s", full_param_name.c_str(), xml_string.c_str());
			
			// extract limits and velocities from urdf model
			urdf::Model model;
			if (!model.initString(xml_string))
			{
				ROS_ERROR("Failed to parse urdf file");
				exit(2);
			}
			ROS_INFO("Successfully parsed urdf file");

			/** @todo: check if yaml parameter file fits to urdf model */

			// get MaxVel out of urdf model
			std::vector<double> MaxVel;
			MaxVel.resize(ModIds_param_.size());
			for (int i = 0; i<ModIds_param_.size(); i++ )
			{
				MaxVel[i] = model.getJoint(JointNames_[i].c_str())->limits->velocity;
				//std::cout << "MaxVel[" << JointNames_[i].c_str() << "] = " << MaxVel[i] << std::endl;
			}
			PCubeParams_->SetMaxVel(MaxVel);
			
			// get LowerLimits out of urdf model
			std::vector<double> LowerLimits;
			LowerLimits.resize(ModIds_param_.size());
			for (int i = 0; i<ModIds_param_.size(); i++ )
			{
				LowerLimits[i] = model.getJoint(JointNames_[i].c_str())->limits->lower;
				std::cout << "LowerLimits[" << JointNames_[i].c_str() << "] = " << LowerLimits[i] << std::endl;
			}
			PCubeParams_->SetLowerLimits(LowerLimits);

			// get UpperLimits out of urdf model
			std::vector<double> UpperLimits;
			UpperLimits.resize(ModIds_param_.size());
			for (int i = 0; i<ModIds_param_.size(); i++ )
			{
				UpperLimits[i] = model.getJoint(JointNames_[i].c_str())->limits->upper;
				std::cout << "UpperLimits[" << JointNames_[i].c_str() << "] = " << UpperLimits[i] << std::endl;
			}
			PCubeParams_->SetUpperLimits(UpperLimits);

			// get UpperLimits out of urdf model
			std::vector<double> Offsets;
			Offsets.resize(ModIds_param_.size());
			for (int i = 0; i<ModIds_param_.size(); i++ )
			{
				Offsets[i] = model.getJoint(JointNames_[i].c_str())->calibration->rising.get()[0];
				std::cout << "Offset[" << JointNames_[i].c_str() << "] = " << Offsets[i] << std::endl;
			}
			PCubeParams_->SetAngleOffsets(Offsets);
			
			cmd_vel_.resize(ModIds_param_.size());
			newvel_ = false;
		}

		/*!
		* \brief Destructor for PowercubeChainNode class.
		*/
		~PowercubeChainNode()
		{
		}

		/*!
		* \brief Executes the callback from the actionlib.
		*
		* Set the current velocity target.
		* \param msg JointTrajectory
		*/
		void topicCallback_DirectCommand(const trajectory_msgs::JointTrajectory::ConstPtr& msg)
		{
			ROS_DEBUG("Received new direct command");
			newvel_ = true;
			cmd_vel_ = msg->points[0].velocities;
		}

		/*!
		* \brief Executes the callback from the actionlib.
		*
		* Set the current goal to aborted after receiving a new goal and write new goal to a member variable. Wait for the goal to finish and set actionlib status to succeeded.
		* \param goal JointTrajectoryGoal
		*/
		void executeCB(const pr2_controllers_msgs::JointTrajectoryGoalConstPtr &goal)
		{
			ROS_INFO("Received new goal trajectory with %d points",goal->trajectory.points.size());
			if (!isInitialized_)
			{
				ROS_ERROR("%s: Rejected, powercubes not initialized", action_name_.c_str());
				as_.setAborted();
				return;
			}
			// saving goal into local variables
			traj_ = goal->trajectory;
			traj_point_nr_ = 0;
			traj_point_ = traj_.points[traj_point_nr_];
			finished_ = false;
			
			// stoping arm to prepare for new trajectory
			std::vector<double> VelZero;
			VelZero.resize(ModIds_param_.size());
			PCube_->MoveVel(VelZero);

			// check that preempt has not been requested by the client
			if (as_.isPreemptRequested())
			{
				ROS_INFO("%s: Preempted", action_name_.c_str());
				// set the action state to preempted
				as_.setPreempted();
			}
			
			usleep(500000); // needed sleep until powercubes starts to change status from idle to moving
			
			while(finished_ == false)
			{
				if (as_.isNewGoalAvailable())
				{
					ROS_WARN("%s: Aborted", action_name_.c_str());
					as_.setAborted();
					return;
				}
		   		usleep(10000);
				//feedback_ = 
				//as_.send feedback_
			}

			// set the action state to succeed			
			//result_.result.data = "executing trajectory";
			ROS_INFO("%s: Succeeded", action_name_.c_str());
			// set the action state to succeeded
			as_.setSucceeded(result_);
		}

		/*!
		* \brief Executes the service callback for init.
		*
		* Connects to the hardware and initialized it.
		* \param req Service request
		* \param res Service response
		*/
		bool srvCallback_Init(	cob_srvs::Trigger::Request &req,
								cob_srvs::Trigger::Response &res )
		{
			if (isInitialized_ == false)
			{
				ROS_INFO("...initializing powercubes...");
			// init powercubes 
			if (PCube_->Init(PCubeParams_))
			{
				ROS_INFO("Initializing succesfull");
				isInitialized_ = true;
				res.success = 0; // 0 = true, else = false
			}
			else
			{
				ROS_ERROR("Initializing powercubes not succesfull. error: %s", PCube_->getErrorMessage().c_str());
				res.success = 1; // 0 = true, else = false
				res.errorMessage.data = PCube_->getErrorMessage();
			}
			}
			else
			{
				ROS_ERROR("...powercubes already initialized...");		        
				res.success = 1;
				res.errorMessage.data = "powercubes already initialized";
			}

			return true;
		}

		/*!
		* \brief Executes the service callback for stop.
		*
		* Stops all hardware movements.
		* \param req Service request
		* \param res Service response
		*/
		bool srvCallback_Stop(	cob_srvs::Trigger::Request &req,
								cob_srvs::Trigger::Response &res )
		{
			ROS_INFO("Stopping powercubes");
			newvel_ = false;
	
			// set current trajectory to be finished
			traj_point_nr_ = traj_.points.size();
	
			// stopping all arm movements
			if (PCube_->Stop())
			{
				ROS_INFO("Stopping powercubes succesfull");
				res.success = 0; // 0 = true, else = false
			}
			else
			{
				ROS_ERROR("Stopping powercubes not succesfull. error: %s", PCube_->getErrorMessage().c_str());
				res.success = 1; // 0 = true, else = false
				res.errorMessage.data = PCube_->getErrorMessage();
			}
			return true;
		}

		/*!
		* \brief Executes the service callback for recover.
		*
		* Recovers the driver after an emergency stop.
		* \param req Service request
		* \param res Service response
		*/
		bool srvCallback_Recover(	cob_srvs::Trigger::Request &req,
									cob_srvs::Trigger::Response &res )
		{
			if (isInitialized_ == true)
			{
		   		ROS_INFO("Recovering powercubes");
		
				// stopping all arm movements
				if (PCube_->Stop())
				{
					ROS_INFO("Recovering powercubes succesfull");
					res.success = 0; // 0 = true, else = false
				}
				else
				{
					ROS_ERROR("Recovering powercubes not succesfull. error: %s", PCube_->getErrorMessage().c_str());
					res.success = 1; // 0 = true, else = false
					res.errorMessage.data = PCube_->getErrorMessage();
				}
			}
			else
			{
				ROS_ERROR("...powercubes already recovered...");
				res.success = 1;
				res.errorMessage.data = "powercubes already recovered";
			}

			return true;
		}

		/*!
		* \brief Executes the service callback for set_operation_mode.
		*
		* Changes the operation mode.
		* \param req Service request
		* \param res Service response
		*/
		bool srvCallback_SetOperationMode(	cob_srvs::SetOperationMode::Request &req,
											cob_srvs::SetOperationMode::Response &res )
		{
			ROS_INFO("Set operation mode to [%s]", req.operationMode.data.c_str());
			n_.setParam("OperationMode", req.operationMode.data.c_str());
			res.success = 0; // 0 = true, else = false
			return true;
		}

		/*!
		* \brief Routine for publishing joint_states.
		*
		* Gets current positions and velocities from the hardware and publishes them as joint_states.
		*/
		void publishJointState()
		{
			if (isInitialized_ == true)
			{
				// create joint_state message
				int DOF = ModIds_param_.size();
				std::vector<double> ActualPos;
				std::vector<double> ActualVel;
				ActualPos.resize(DOF);
				ActualVel.resize(DOF);

				PCube_->getConfig(ActualPos);
				PCube_->getJointVelocities(ActualVel);

				sensor_msgs::JointState msg;
				msg.header.stamp = ros::Time::now();
				msg.name.resize(DOF);
				msg.position.resize(DOF);
				msg.velocity.resize(DOF);

				msg.name = JointNames_;

				for (int i = 0; i<DOF; i++ )
				{
					msg.position[i] = ActualPos[i];
					msg.velocity[i] = ActualVel[i];
					//std::cout << "Joint " << msg.name[i] <<": pos="<<  msg.position[i] << "vel=" << msg.velocity[i] << std::endl;
				}
		
				// publish message
				topicPub_JointState_.publish(msg);

				pr2_controllers_msgs::JointTrajectoryControllerState controllermsg;
				controllermsg.header.stamp = ros::Time::now();
				controllermsg.joint_names.resize(DOF);
				controllermsg.actual.positions.resize(DOF);
				controllermsg.actual.velocities.resize(DOF);

				controllermsg.joint_names = JointNames_;

				for (int i = 0; i<DOF; i++ )
				{
					controllermsg.actual.positions[i] = ActualPos[i];
					controllermsg.actual.velocities[i] = ActualVel[i];
					//std::cout << "Joint " << msg.name[i] <<": pos="<<  msg.position[i] << "vel=" << msg.velocity[i] << std::endl;
				}
				topicPub_ControllerState_.publish(controllermsg);

			}
		}

		/*!
		* \brief Routine for updating command to the powercubes.
		*
		* Depending on the operation mode (position/velocity) either position or velocity goals are sent to the hardware.
		*/
		void updatePCubeCommands()
		{
			if (isInitialized_ == true)
			{
				std::string operationMode;
				n_.getParam("OperationMode", operationMode);
				if (operationMode == "position")
				{
					ROS_DEBUG("moving powercubes in position mode");
					if (PCube_->statusMoving() == false)
					{
						//feedback_.isMoving = false;
			
						ROS_DEBUG("next point is %d from %d",traj_point_nr_,traj_.points.size());
			
						if (traj_point_nr_ < traj_.points.size())
						{
							// if powercubes are not moving and not reached last point of trajectory, then send new target point
							ROS_DEBUG("...moving to trajectory point[%d]",traj_point_nr_);
							traj_point_ = traj_.points[traj_point_nr_];
							PCube_->MoveJointSpaceSync(traj_point_.positions);
							traj_point_nr_++;
							//feedback_.isMoving = true;
							//feedback_.pointNr = traj_point_nr;
							//as_.publishFeedback(feedback_);
						}
						else
						{
							ROS_DEBUG("...reached end of trajectory");
							finished_ = true;
						}
					}
					else
					{
						ROS_DEBUG("...powercubes still moving to point[%d]",traj_point_nr_);
					}
				}
				else if (operationMode == "velocity")
				{
					ROS_DEBUG("moving powercubes in velocity mode");
					if(newvel_)
					{	
						ROS_INFO("MoveVel Call");
						PCube_->MoveVel(cmd_vel_);
						newvel_ = false;
					}
				}
				else
				{
					ROS_ERROR("powercubes neither in position nor in velocity mode. OperationMode = [%s]", operationMode.c_str());
				}
			}
			else
			{
				ROS_DEBUG("powercubes not initialized");
			}
		}
}; //PowercubeChainNode

/*!
* \brief Main loop of ROS node.
*
* Running with a specific frequency defined by loop_rate.
*/
int main(int argc, char** argv)
{
	// initialize ROS, specify name of node
	ros::init(argc, argv, "powercube_chain");

	// create class
	PowercubeChainNode pc_node("joint_trajectory_action");

	// main loop
	ros::Rate loop_rate(10); // Hz
	while(pc_node.n_.ok())
	{
		// publish JointState
		pc_node.publishJointState();

		// update commands to powercubes
		pc_node.updatePCubeCommands();

		// read parameter
		std::string operationMode;
		pc_node.n_.getParam("OperationMode", operationMode);
		ROS_DEBUG("running with OperationMode [%s]", operationMode.c_str());

		// sleep and waiting for messages, callbacks 
		ros::spinOnce();
		loop_rate.sleep();
	}

	return 0;
}
