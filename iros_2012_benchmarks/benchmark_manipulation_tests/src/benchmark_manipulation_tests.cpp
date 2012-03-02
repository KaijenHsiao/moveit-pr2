#include <benchmark_manipulation_tests/benchmark_manipulation_tests.h>
#include <sys/stat.h>
#include <dirent.h>

BenchmarkManipulationTests::BenchmarkManipulationTests() : ph_("~")
{
  action_list_.push_back("nothing");
  action_list_.push_back("attach");
  action_list_.push_back("detach");
  action_map_["nothing"] = 0;
  action_map_["attach"] = 1;
  action_map_["detach"] = 2;
  group_name_ = "right_arm";
  planner_id_ = "sbpl_arm_planner";
  experiment_type_ = 1;
  trajectory_folder_path_ = "/tmp";
  use_current_state_as_start_ = false;

  benchmark_service_name_="/benchmark_planning_problem";
  world_frame_="map";
  robot_model_root_frame_="odom";
  spine_frame_="torso_lift_link";

  pviz_.setReferenceFrame("base_footprint");
  ROS_INFO("[exp] Waiting for the benchmark service..Did you start it up first?.");
  ros::service::waitForService(benchmark_service_name_);
  benchmark_client_ = nh_.serviceClient<moveit_msgs::ComputePlanningBenchmark>(benchmark_service_name_, true);
  ROS_INFO("[exp] Connected to the benchmark service.");


  psm_ = new planning_scene_monitor::PlanningSceneMonitor("robot_description");
}

bool BenchmarkManipulationTests::getParams()
{
  XmlRpc::XmlRpcValue plist;
  std::string p;

  ph_.param<std::string>("known_objects_filename",known_objects_filename_, "");
  ph_.param<std::string>("trajectory_folder_path",trajectory_folder_path_, "/tmp");
  time_t clock;
  time(&clock);
  std::string time(ctime(&clock));;
  time.erase(time.size()-1, 1);
  trajectory_files_path_ = trajectory_folder_path_ + "/" + time;
  if(mkdir(trajectory_files_path_.c_str(),  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0)
    ROS_INFO("Successfully created the trajectory folder: %s", trajectory_files_path_.c_str());
  else
    ROS_WARN("Failed to create the trajectory folder: %s", trajectory_files_path_.c_str());
  
  if(ph_.hasParam("goal_tolerance/xyz") && ph_.hasParam("goal_tolerance/rpy"))
  {
    ph_.getParam("goal_tolerance/xyz", plist);
    std::stringstream ss(plist);
    while(ss >> p)
      goal_tolerance_.push_back(atof(p.c_str()));

    ph_.getParam("goal_tolerance/rpy", plist);
    std::stringstream ss1(plist);
    while(ss1 >> p)
      goal_tolerance_.push_back(atof(p.c_str()));
  }
  else
  {
    goal_tolerance_.resize(6,0.02);
    goal_tolerance_[3] = 0.05;
    goal_tolerance_[4] = 0.05;
    goal_tolerance_[5] = 0.05;
  }

  ph_.param<std::string>("benchmark_service", benchmark_service_name_, "/benchmark_planning_problem"); 
  ph_.param<std::string>("world_frame", world_frame_, "map");
  ph_.param<std::string>("robot_model_root_frame", robot_model_root_frame_, "odom");
  ph_.param<std::string>("spine_frame", spine_frame_, "torso_lift_link");
  
  if(ph_.hasParam("object_pose_in_gripper"))
  {
    rarm_object_offset_.clear();
    larm_object_offset_.clear();
    ph_.getParam("object_pose_in_gripper/right/xyz", plist);
    std::stringstream ss(plist);
    while(ss >> p)
      rarm_object_offset_.push_back(atof(p.c_str()));

    ph_.getParam("object_pose_in_gripper/right/rpy", plist); 
    std::stringstream ss1(plist);
    while(ss1 >> p)
      rarm_object_offset_.push_back(atof(p.c_str()));
    
    ph_.getParam("object_pose_in_gripper/left/xyz", plist);
    std::stringstream ss2(plist);
    while(ss2 >> p)
      larm_object_offset_.push_back(atof(p.c_str()));

    ph_.getParam("object_pose_in_gripper/left/rpy", plist); 
    std::stringstream ss3(plist);
    while(ss3 >> p)
      larm_object_offset_.push_back(atof(p.c_str()));
  }
  
  tf::Quaternion btoffset;
  rarm_object_pose_.position.x = rarm_object_offset_[0];
  rarm_object_pose_.position.y = rarm_object_offset_[1];
  rarm_object_pose_.position.z = rarm_object_offset_[2];
  larm_object_pose_.position.x = larm_object_offset_[0];
  larm_object_pose_.position.y = larm_object_offset_[1];
  larm_object_pose_.position.z = larm_object_offset_[2];
  btoffset.setRPY(rarm_object_offset_[3],rarm_object_offset_[4],rarm_object_offset_[5]);
  tf::quaternionTFToMsg(btoffset,rarm_object_pose_.orientation);
  btoffset.setRPY(larm_object_offset_[3],larm_object_offset_[4],larm_object_offset_[5]);
  tf::quaternionTFToMsg(btoffset,larm_object_pose_.orientation);

  if(ph_.hasParam("use_current_pose_as_start_state"))
    ph_.getParam("use_current_pose_as_start_state", use_current_state_as_start_);
 
  if(ph_.hasParam("group_name"))
  {
    ph_.getParam("group_name", group_name_);
    if(group_name_.compare("arms") == 0)
      experiment_type_ = 2;
    else if(group_name_.compare("right_arm") == 0)
      experiment_type_ = 1;
    else
    {
      ROS_ERROR("[exp] This infrastructure only supports group_names of {'right_arm', 'arms'}. Exiting.");
      return false;
    }
  }
  else
  {
    group_name_ = "right_arm";
    experiment_type_ = 1;
  }
  
  if(ph_.hasParam("start_state"))
  {
    start_pose_.rangles.clear();
    start_pose_.langles.clear();
    std::vector<double> bpose;
    ph_.getParam("start_state/right", plist);
    std::stringstream ss(plist);
    while(ss >> p)
      start_pose_.rangles.push_back(atof(p.c_str()));
    ph_.getParam("start_state/left", plist);
    std::stringstream ss1(plist);
    ss.str(plist);
    while(ss1 >> p)
      start_pose_.langles.push_back(atof(p.c_str()));
    ph_.getParam("start_state/base", plist);
    std::stringstream ss2(plist);
    while(ss2 >> p)
      bpose.push_back(atof(p.c_str()));
    if(bpose.size() == 3)
    {
      start_pose_.body.x = bpose[0];
      start_pose_.body.y = bpose[1];
      start_pose_.body.theta = bpose[2];
    }
    ph_.getParam("start_state/spine", start_pose_.body.z);  
  }

  if(ph_.hasParam("planner_interfaces"))
  {
    ph_.getParam("planner_interfaces", plist);
    std::string planner_list = std::string(plist);
    std::stringstream ss(planner_list);
    while(ss >> p)
      planner_interfaces_.push_back(p);
  }

  if(goal_tolerance_.size() < 6 || 
      start_pose_.rangles.size() < 7 ||
      start_pose_.langles.size() < 7 ||
      rarm_object_offset_.size() < 6 ||
      larm_object_offset_.size() < 6)
    return false;

  if(!getLocations() || !getExperiments())
    return false;

  return true;
}

bool BenchmarkManipulationTests::getLocations()
{
  XmlRpc::XmlRpcValue loc_list;
  geometry_msgs::Pose p;
  std::string name;
  std::string loc_name = "locations";

  if(!ph_.hasParam(loc_name))
  {
    ROS_WARN("[exp] No list of locations found on the param server.");
    return false;
  }
  ph_.getParam(loc_name, loc_list);

  if(loc_list.getType() != XmlRpc::XmlRpcValue::TypeArray)
  {
    ROS_WARN("[exp] Location list is not an array. Something is wrong...exiting.");
    return false;
  }

  if(loc_list.size() == 0)
  {
    ROS_ERROR("[exp] List of locations is empty.");
    return false;
  }

  for(int i = 0; i < loc_list.size(); ++i)
  {
    if(!loc_list[i].hasMember("name"))
    {
      ROS_ERROR("Each location must have a name.");
      return false;
    }
    name = std::string(loc_list[i]["name"]);
    std::stringstream ss(loc_list[i]["pose"]);
    std::string p;
    while(ss >> p)
      loc_map_[name].push_back(atof(p.c_str()));
  }

  ROS_INFO("[exp] Successfully fetched %d locations from param server.", int(loc_list.size()));
  return true;
}

bool BenchmarkManipulationTests::getExperiments()
{
  XmlRpc::XmlRpcValue exp_list;
  Experiment e;
  std::string exp_name = "experiments";
  XmlRpc::XmlRpcValue plist;
  std::string p;

  if(!ph_.hasParam(exp_name))
  {
    ROS_WARN("No list of experiments found on the param server.");
    return false;
  }
  ph_.getParam(exp_name, exp_list);

  if(exp_list.getType() != XmlRpc::XmlRpcValue::TypeArray)
  {
    ROS_WARN("Experiment list is not an array. Something is wrong...exiting.");
    return false;
  }

  if(exp_list.size() == 0)
  {
    ROS_ERROR("List of experiments is empty.");
    return false;
  }

  for(int i = 0; i < exp_list.size(); ++i)
  {
    if(!exp_list[i].hasMember("name"))
    {
      ROS_ERROR("Each experiment must have a name.");
      return false;
    }
    e.name = std::string(exp_list[i]["name"]);

    if(!exp_list[i].hasMember("goal"))
    {
      ROS_ERROR("Each experiment must have a goal....duh.");
      return false;
    }
    e.goal = std::string(exp_list[i]["goal"]);

    if(!exp_list[i].hasMember("pre_action"))
      e.pre_action = 0;
    else
      e.pre_action = action_map_[exp_list[i]["pre_action"]];

    if(!exp_list[i].hasMember("post_action"))
      e.post_action = 0;
    else
      e.post_action = action_map_[exp_list[i]["post_action"]];

    if(!exp_list[i].hasMember("sound_bite"))
      e.sound_bite = "";
    else
      e.sound_bite = std::string(exp_list[i]["sound_bite"]);

    if(exp_list[i].hasMember("start"))
    {
      e.start.rangles.clear();
      e.start.langles.clear();
      std::vector<double> bpose;
      plist = exp_list[i]["start"]["right"];
      std::stringstream ss(plist);
      while(ss >> p)
        e.start.rangles.push_back(atof(p.c_str()));
      
      plist = exp_list[i]["start"]["left"];
      std::stringstream ss1(plist);
      while(ss1 >> p)
        e.start.langles.push_back(atof(p.c_str()));
      
      plist = exp_list[i]["start"]["base"];
      std::stringstream ss2(plist);
      while(ss2 >> p)
        bpose.push_back(atof(p.c_str()));
      if(bpose.size() == 3)
      {
        e.start.body.x = bpose[0];
        e.start.body.y = bpose[1];
        e.start.body.theta = bpose[2];
      }
      e.start.body.z = double(exp_list[i]["start"]["spine"]);
    }
    else
    {
      if(!use_current_state_as_start_)
      {
        ROS_ERROR("[exp] No start state defined for %s and it isn't configured to use the current state as the start state.",e.name.c_str());
        return false; 
      }
      else  
        ROS_DEBUG("No start state defined for %s but it's OK because it's configured to use the current state as the start.",e.name.c_str());
    }

    ROS_INFO("Adding experiment: %s", e.name.c_str());
    exp_map_[e.name] = e;
  }

  return true;  
}

bool BenchmarkManipulationTests::getCollisionObjects(std::string filename, std::vector<moveit_msgs::CollisionObject> &collision_objects)
{
  int num_obs;
  char sTemp[1024];
  std::vector<std::vector<double> > objects;
  std::vector<std::string> object_ids;
  moveit_msgs::CollisionObject object;
  collision_objects.clear();

  FILE* fCfg = fopen(filename.c_str(), "r");
  if(fCfg == NULL)
    return false;

  // get number of objects
  if(fscanf(fCfg,"%d",&num_obs) < 1)
    ROS_INFO("[exp] Parsed string has length < 1.(number of obstacles)\n");

  ROS_INFO("[exp] Parsing collision object file with %i objects.",num_obs);

  //get {x y z dimx dimy dimz} for each object
  objects.resize(num_obs, std::vector<double>(10,0.0));
  object_ids.clear();
  for (int i=0; i < num_obs; ++i)
  {
    if(fscanf(fCfg,"%s",sTemp) < 1)
      ROS_INFO("[exp] Parsed string has length < 1.\n");
    object_ids.push_back(sTemp);

    for(int j=0; j < 10; ++j)
    {
      if(fscanf(fCfg,"%s",sTemp) < 1)
        ROS_INFO("[exp] Parsed string has length < 1. (object parameters for %s)\n", object_ids.back().c_str());
      if(!feof(fCfg) && strlen(sTemp) != 0)
        objects[i][j] = atof(sTemp);
    }
  }

  if(object_ids.size() != objects.size())
  {
    ROS_INFO("object id list is not same length as object list. exiting.");
    return false;
  }

  object.shapes.resize(1);
  object.poses.resize(1);
  object.shapes[0].dimensions.resize(3);
  object.shapes[0].triangles.resize(4);
  for(size_t i = 0; i < objects.size(); i++)
  {
    object.id = object_ids[i];
    object.operation = moveit_msgs::CollisionObject::ADD;
    object.shapes[0].type = moveit_msgs::Shape::BOX;
    object.header.frame_id = "odom";
    object.header.stamp = ros::Time::now();

    object.poses[0].position.x = objects[i][0];
    object.poses[0].position.y = objects[i][1];
    object.poses[0].position.z = objects[i][2];
    object.poses[0].orientation.x = 0; 
    object.poses[0].orientation.y = 0; 
    object.poses[0].orientation.z = 0; 
    object.poses[0].orientation.w = 1;  

    object.shapes[0].dimensions[0] = objects[i][3];
    object.shapes[0].dimensions[1] = objects[i][4];
    object.shapes[0].dimensions[2] = objects[i][5];

    object.shapes[0].triangles[0] = objects[i][6];
    object.shapes[0].triangles[1] = objects[i][7];
    object.shapes[0].triangles[2] = objects[i][8];
    object.shapes[0].triangles[3] = objects[i][9];

    collision_objects.push_back(object);
    ROS_INFO("[exp] [%d] id: %s xyz: %0.3f %0.3f %0.3f dims: %0.3f %0.3f %0.3f colors: %0.3f %0.3f %0.3f %0.3f",int(i),object_ids[i].c_str(),objects[i][0],objects[i][1],objects[i][2],objects[i][3],objects[i][4],objects[i][5],objects[i][6],objects[i][7],objects[i][8],objects[i][9]);
  }
  return true;
}

bool BenchmarkManipulationTests::getAttachedObject(std::string object_file, geometry_msgs::Pose rarm_object_pose, moveit_msgs::AttachedCollisionObject &att_object)
{
  char sTemp[1024];
  float temp[6];
 
  att_object.link_name = "r_gripper_r_finger_tip_link";
  att_object.touch_links.push_back("r_gripper_palm_link");
  att_object.touch_links.push_back("r_gripper_r_finger_link");
  att_object.touch_links.push_back("r_gripper_l_finger_link");
  att_object.touch_links.push_back("r_gripper_l_finger_tip_link");
  att_object.touch_links.push_back("l_gripper_palm_link");
  att_object.touch_links.push_back("l_gripper_r_finger_link");
  att_object.touch_links.push_back("l_gripper_l_finger_link");
  att_object.touch_links.push_back("l_gripper_l_finger_tip_link");
  att_object.touch_links.push_back("l_gripper_r_finger_tip_link");
  att_object.object.header.frame_id = "e_wrist_roll_link";
  att_object.object.operation = moveit_msgs::CollisionObject::ADD;
  att_object.object.header.stamp = ros::Time::now();
  att_object.object.shapes.resize(1);
  att_object.object.shapes[0].type = moveit_msgs::Shape::BOX;
  att_object.object.poses.resize(1);
  att_object.object.poses[0] = rarm_object_pose;

  FILE* fid = fopen(object_file.c_str(), "r");
  if(fid == NULL)
  {
    ROS_ERROR("[exp] Failed to open object file. (%s)", object_file.c_str());
    return false;
  }

  // object name
  if(fscanf(fid,"%s",sTemp) < 1)
    ROS_WARN("Parsed string has length < 1. (%s)", sTemp);
  att_object.object.id = sTemp;

  // dims
  if(fscanf(fid,"%s",sTemp) < 1)
    ROS_WARN("Parsed string has length < 1. (%s)", sTemp); 
  if(strcmp(sTemp, "dims:") == 0)
  {
    if(fscanf(fid,"%f %f %f",&(temp[0]),&(temp[1]),&(temp[2])) < 1)
      ROS_WARN("Failed to parse dims.");
    att_object.object.shapes[0].dimensions.resize(3,0);
    att_object.object.shapes[0].dimensions[0] = temp[0];
    att_object.object.shapes[0].dimensions[1] = temp[1];
    att_object.object.shapes[0].dimensions[2] = temp[2];  
  }
  // xyz in r_wrist_roll_link
  if(strcmp(sTemp, "xyz:") == 0)
  {
    if(fscanf(fid,"%f %f %f",&(temp[0]),&(temp[1]),&(temp[2])) < 1)
      ROS_WARN("Failed to parse xyz.");
    att_object.object.poses[0].position.x = temp[0];
    att_object.object.poses[0].position.y = temp[1];
    att_object.object.poses[0].position.z = temp[2];
  }
  return true;
}

bool BenchmarkManipulationTests::requestPlan(RobotPose &start_state, std::string name)
{
  moveit_msgs::ComputePlanningBenchmark::Request req;
  moveit_msgs::ComputePlanningBenchmark::Response res;

  req.average_count = 10;
  req.filename = "/u/bcohen/Desktop/bens_res.log";
  psm_->getPlanningScene()->getAllowedCollisionMatrix().getMessage(req.scene.allowed_collision_matrix);

  req.scene.robot_state.joint_state.header.frame_id = robot_model_root_frame_;
  req.scene.robot_state.joint_state.header.stamp = ros::Time::now();
  req.scene.robot_state.joint_state.name.resize(1);
  req.scene.robot_state.joint_state.name[0] = spine_frame_;
  req.scene.robot_state.joint_state.position.resize(1);
  req.scene.robot_state.joint_state.position[0] = start_state.body.z;
ROS_ERROR("torso start: %f", req.scene.robot_state.joint_state.position[0]);

  // fill in collision objects
  if(!known_objects_filename_.empty())
  {
    if(!getCollisionObjects(known_objects_filename_, req.scene.world.collision_objects))
    {
      ROS_ERROR("[exp] Failed to get the collision objects from the file.");
      return false;
    }
  }

  // fill in attached object
  if(exp_map_[name].pre_action == action_map_["attach"])
  {
    req.scene.attached_collision_objects.resize(1);
    if(!getAttachedObject(attached_object_filename_, rarm_object_pose_, req.scene.attached_collision_objects[0]))
    {
      ROS_ERROR("[exp] Failed to add the attached object.");
      return false;
    }
  }
  else if(exp_map_[name].pre_action == action_map_["detach"])
  {
    req.scene.attached_collision_objects.resize(1);
    if(!getAttachedObject(attached_object_filename_, rarm_object_pose_, req.scene.attached_collision_objects[0]))
    {
      ROS_ERROR("[exp] Failed to remove the attached object.");
      return false;
    }
    req.scene.attached_collision_objects[0].object.operation = moveit_msgs::CollisionObject::REMOVE;
  }

  ROS_INFO("[exp] This experiment is of type %d, for group: %s", experiment_type_, group_name_.c_str());
  // fill in goal
  if(experiment_type_ == 1)
    fillSingleArmPlanningRequest(start_state,name,req.motion_plan_request);
  else
    fillDualArmPlanningRequest(start_state,name,req.motion_plan_request);

  if(benchmark_client_.call(req, res))
  {
    ROS_INFO("[exp] Planner returned status code: %d.", res.error_code.val); 

  }
  else
  {
    ROS_ERROR("[exp] Planning service failed to respond. Exiting.");
    return false;
  }

  if(!res.trajectory.empty())
  {
    if(!setRobotPoseFromTrajectory(res.trajectory[0], res.trajectory_start[0], current_pose_))
    {
      ROS_ERROR("[exp] Failed to set the current robot pose from the trajectory found.");
      return false;
    }
  }

  printRobotPose(current_pose_, "pose_after_trajectory");  
  return true;
}

bool BenchmarkManipulationTests::runExperiment(std::string name)
{
/*
  if(use_current_state_as_start_)
    startCompleteExperimentFile();
*/
  RobotPose start;
  std::vector<std::vector<double> > traj;
  
  ROS_INFO("[exp] Trying to plan: %s", name.c_str());
/*
  if(use_current_state_as_start_)
  {
    if(std::distance(exp_map_.begin(), exp_map_[name]) == 0)
      start = start_pose_;
    else
      start = current_pose_;
  }
  else
    start = iter->second.start;

  if(use_current_state_as_start_)
    writeCompleteExperimentFile(iter->second,start);
*/

  start = start_pose_;
  printRobotPose(start, "start");
  ROS_INFO("[exp]  goal: %s", exp_map_[name].goal.c_str());
  if(!requestPlan(start, name))
  {
    ROS_ERROR("[exp] %s failed to plan.", name.c_str());
    return false; 
  }
  else
    ROS_INFO("[exp] It's a planning miracle!");

  visualizeLocations();
  return true;
}

bool BenchmarkManipulationTests::performAllExperiments()
{
  if(use_current_state_as_start_)
    startCompleteExperimentFile();

  RobotPose start;
  std::vector<std::vector<double> > traj;
  for(std::map<std::string,Experiment>::iterator iter = exp_map_.begin(); iter != exp_map_.end(); ++iter)
  {
    ROS_INFO("[exp] Trying to plan: %s (%d)", iter->second.name.c_str(), int(std::distance(exp_map_.begin(), iter))+1);
    if(use_current_state_as_start_)
    {
      if(std::distance(exp_map_.begin(), iter) == 0)
        start = start_pose_;
      else
        start = current_pose_;
    }
    else
      start = iter->second.start;

    if(use_current_state_as_start_)
      writeCompleteExperimentFile(iter->second,start);

    printRobotPose(start, "start");
    ROS_INFO("[exp]  goal: %s", iter->second.goal.c_str());
    if(!requestPlan(start, iter->second.name))
    {
      ROS_ERROR("[exp] %s failed to plan.", iter->second.name.c_str());
      return false; 
    }
    else
      ROS_INFO("[exp] It's a planning miracle!");

    visualizeLocations();
  }
  return true;
}

void BenchmarkManipulationTests::visualizeStartPose()
{
  visualizeRobotPose(start_pose_, "start", 0);
}

void BenchmarkManipulationTests::visualizeRobotPose(RobotPose &pose, std::string name, int id)
{
  pviz_.visualizeRobot(pose.rangles, pose.langles, pose.body, 120, name, id);
}

void BenchmarkManipulationTests::visualizeLocations()
{
  std::vector<std::vector<double> > poses;
  poses.resize(std::distance(loc_map_.begin(),loc_map_.end()));
  for(std::map<std::string,std::vector<double> >::iterator iter = loc_map_.begin(); iter != loc_map_.end(); ++iter)
  {
    int i = std::distance(loc_map_.begin(), iter);
    poses[i].resize(6,0.0);
    poses[i][0] = iter->second.at(0);
    poses[i][1] = iter->second.at(1);
    poses[i][2] = iter->second.at(2);
    poses[i][5] = iter->second.at(3);
  }

  ROS_INFO("[exp] Visualizing %d locations.", int(poses.size()));
  pviz_.visualizePoses(poses);
}

void BenchmarkManipulationTests::startCompleteExperimentFile(){
  FILE* fout = fopen("complete_experiments_with_starts.yaml","w");
  fprintf(fout,"goal_tolerance:\n  xyz: 0.02 0.02 0.02\n rpy: 0.05 0.05 0.05\n\n");
  fprintf(fout,"object_pose_in_gripper:\n  right:\n    xyz: -0.20 -0.1 0.0\n    rpy: 0.0 0.0 0.0\n  left:\n    xyz: -0.20 0.1 0.0\n    rpy: 0.0 0.0 0.0\n\n");
  fprintf(fout,"use_current_pose_as_start_state: false\n");
  fprintf(fout,"start_state:\n  right: 0.0 0.018 0.00 -0.43 0.00 -0.00 0.0\n  left: 0.0 0.018 0.00 -0.43 0.00 -0.00 0.0\n  base: 1.9 0.6 1.57\n  spine: 0.0\n\n");
  fprintf(fout,"experiments:\n\n");
  fclose(fout);
}

void BenchmarkManipulationTests::writeCompleteExperimentFile(Experiment e, RobotPose start){
  FILE* fout = fopen("completeExperiment.yaml","a");
  fprintf(fout,"  - name: %s\n",e.name.c_str());
  fprintf(fout,"    goal: %s\n",e.goal.c_str());
  if(e.pre_action==0)
    fprintf(fout,"    pre_action: nothing\n");
  else if(e.pre_action==1)
    fprintf(fout,"    pre_action: attach\n");
  else if(e.pre_action==2)
    fprintf(fout,"    pre_action: detach\n");
  if(e.post_action==0)
    fprintf(fout,"    post_action: nothing\n");
  else if(e.post_action==1)
    fprintf(fout,"    post_action: attach\n");
  else if(e.post_action==2)
    fprintf(fout,"    post_action: detach\n");
  fprintf(fout,"    sound_bite: \"%s\"\n",e.sound_bite.c_str());
  fprintf(fout,"    start:\n");
  fprintf(fout,"      right: %f %f %f %f %f %f %f\n",start.rangles[0],start.rangles[1],start.rangles[2],start.rangles[3],start.rangles[4],start.rangles[5],start.rangles[6]);
  fprintf(fout,"      left: %f %f %f %f %f %f %f\n",start.langles[0],start.langles[1],start.langles[2],start.langles[3],start.langles[4],start.langles[5],start.langles[6]);
  fprintf(fout,"      base: %f %f %f\n",start.body.x,start.body.y,start.body.theta);
  fprintf(fout,"      torso: %f\n",start.body.z);
  fclose(fout);
}

void BenchmarkManipulationTests::printLocations()
{
  if(loc_map_.begin() == loc_map_.end())
  {
    ROS_ERROR("[exp] No locations found.");
    return;
  }
  for(std::map<std::string,std::vector<double> >::const_iterator iter = loc_map_.begin(); iter != loc_map_.end(); ++iter)
  {
    ROS_INFO("name: %s", iter->first.c_str());
    ROS_INFO("  x: % 0.3f  y: % 0.3f  z: % 0.3f theta: % 0.3f", iter->second.at(0), iter->second.at(1), iter->second.at(2), iter->second.at(3));
  }
}

void BenchmarkManipulationTests::printExperiments()
{
  if(exp_map_.begin() == exp_map_.end())
  {
    ROS_ERROR("[exp] No experiments found.");
    return;
  }
  for(std::map<std::string,Experiment>::iterator iter = exp_map_.begin(); iter != exp_map_.end(); ++iter)
  {
    int p = std::distance(exp_map_.begin(), iter);
    ROS_INFO("------------------------------");
    ROS_INFO("[%d] name: %s", p, iter->second.name.c_str());
    ROS_INFO("[%d] goal: %s", p, iter->second.goal.c_str());
    ROS_INFO("[%d] pre_action: %s", p, action_list_[iter->second.pre_action].c_str());
    ROS_INFO("[%d] post_action: %s", p, action_list_[iter->second.post_action].c_str());
    ROS_INFO("[%d] sound_bite: %s", p, iter->second.sound_bite.c_str());
    if(!use_current_state_as_start_)
    {
      ROS_INFO("[%d] start:", p);
      ROS_INFO("[%d]   right: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", p, iter->second.start.rangles[0], iter->second.start.rangles[1], iter->second.start.rangles[2], iter->second.start.rangles[3], iter->second.start.rangles[4], iter->second.start.rangles[5], iter->second.start.rangles[6]);
      ROS_INFO("[%d]    left: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", p, iter->second.start.langles[0], iter->second.start.langles[1], iter->second.start.langles[2], iter->second.start.langles[3], iter->second.start.langles[4], iter->second.start.langles[5], iter->second.start.langles[6]);
      ROS_INFO("[%d]    base: % 0.3f % 0.3f % 0.3f", p, iter->second.start.body.x, iter->second.start.body.y, iter->second.start.body.theta);
      ROS_INFO("[%d]   torso: % 0.3f", p, iter->second.start.body.z);
    }
  }
}

void BenchmarkManipulationTests::printRobotPose(RobotPose &pose, std::string name)
{
  ROS_INFO("[%s] right: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", name.c_str(), pose.rangles[0], pose.rangles[1], pose.rangles[2], pose.rangles[3], pose.rangles[4], pose.rangles[5], pose.rangles[6]);
  ROS_INFO("[%s]  left: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", name.c_str(), pose.langles[0], pose.langles[1], pose.langles[2], pose.langles[3], pose.langles[4], pose.langles[5], pose.langles[6]);
  ROS_INFO("[%s]  base: % 0.3f % 0.3f % 0.3f", name.c_str(), pose.body.x, pose.body.y, pose.body.theta);
  ROS_INFO("[%s] torso: % 0.3f", name.c_str(), pose.body.z);
}

void BenchmarkManipulationTests::printParams()
{
  for(size_t i = 0; i < planner_interfaces_.size(); ++i)
    ROS_INFO("   [planner_interface] %d/%d %s", int(i+1), int(planner_interfaces_.size()), planner_interfaces_[i].c_str());

  ROS_INFO("   [goal tolerance] x: % 0.3f  y: % 0.3f  z: % 0.3f  roll: % 0.3f  pitch: %0.3f  yaw: %0.3f", goal_tolerance_[0], goal_tolerance_[1], goal_tolerance_[2], goal_tolerance_[3], goal_tolerance_[4], goal_tolerance_[5]);
  ROS_INFO("[right object pose] x: % 0.3f  y: % 0.3f  z: % 0.3f  r: % 0.3f  p: % 0.3f  y: % 0.3f", rarm_object_offset_[0], rarm_object_offset_[1], rarm_object_offset_[2], rarm_object_offset_[3], rarm_object_offset_[4], rarm_object_offset_[5]);
  ROS_INFO(" [left object pose] x: % 0.3f  y: % 0.3f  z: % 0.3f  r: % 0.3f  p: % 0.3f  y: % 0.3f", larm_object_offset_[0], larm_object_offset_[1], larm_object_offset_[2], larm_object_offset_[3], larm_object_offset_[4], larm_object_offset_[5]);
  ROS_INFO("      [start state] right: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", start_pose_.rangles[0], start_pose_.rangles[1], start_pose_.rangles[2], start_pose_.rangles[3], start_pose_.rangles[4], start_pose_.rangles[5], start_pose_.rangles[6]);
  ROS_INFO("      [start state]  left: % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f % 0.3f", start_pose_.langles[0], start_pose_.langles[1], start_pose_.langles[2], start_pose_.langles[3], start_pose_.langles[4], start_pose_.langles[5], start_pose_.langles[6]);
  ROS_INFO("      [start state]  base: % 0.3f % 0.3f % 0.3f", start_pose_.body.x, start_pose_.body.y, start_pose_.body.theta);
  ROS_INFO("      [start state] torso: % 0.3f", start_pose_.body.z);

  printLocations();
  printExperiments();
}

void BenchmarkManipulationTests::fillSingleArmPlanningRequest(RobotPose &start_state, std::string name, moveit_msgs::MotionPlanRequest &req)
{
  tf::Quaternion q;
  req.group_name = group_name_;
  req.num_planning_attempts = 1;
  req.allowed_planning_time = ros::Duration(60.0);
  req.planner_id = "";

  // start state
  for(size_t i = 0; i < start_state.rangles.size(); ++i)
    req.start_state.joint_state.position.push_back(start_state.rangles[i]);

  req.start_state.joint_state.name.push_back("r_shoulder_pan_joint");
  req.start_state.joint_state.name.push_back("r_shoulder_lift_joint");
  req.start_state.joint_state.name.push_back("r_upper_arm_roll_joint");
  req.start_state.joint_state.name.push_back("r_elbow_flex_joint");
  req.start_state.joint_state.name.push_back("r_forearm_roll_joint");
  req.start_state.joint_state.name.push_back("r_wrist_flex_joint");
  req.start_state.joint_state.name.push_back("r_wrist_roll_joint");


  req.start_state.joint_state.position.push_back(start_state.body.z);
  req.start_state.joint_state.name.push_back("torso_lift_link");
  ROS_ERROR("[exp] Setting torso_lift_link to %0.3f in the start state.", start_state.body.z);

  // goal pose
  req.goal_constraints.resize(1);
  req.goal_constraints[0].position_constraints.resize(1);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.header.stamp = ros::Time::now();
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.header.frame_id = "odom";

  req.goal_constraints[0].position_constraints[0].link_name = "r_wrist_roll_link";
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.x = loc_map_[exp_map_[name].goal].at(0);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.y = loc_map_[exp_map_[name].goal].at(1);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.z = loc_map_[exp_map_[name].goal].at(2);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.orientation.w = 1;

  req.goal_constraints[0].position_constraints[0].constraint_region_shape.type = moveit_msgs::Shape::BOX;
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[0]);
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[1]);
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[2]);
  req.goal_constraints[0].position_constraints[0].weight = 1.0;

  req.goal_constraints[0].orientation_constraints.resize(1);
  req.goal_constraints[0].orientation_constraints[0].orientation.header.stamp = ros::Time::now();
  req.goal_constraints[0].orientation_constraints[0].orientation.header.frame_id = "odom";
  req.goal_constraints[0].orientation_constraints[0].link_name = "r_wrist_roll_link";

  q.setRPY(loc_map_[exp_map_[name].goal].at(3), loc_map_[exp_map_[name].goal].at(4), loc_map_[exp_map_[name].goal].at(5));
  tf::quaternionTFToMsg(q, req.goal_constraints[0].orientation_constraints[0].orientation.quaternion);

  req.goal_constraints[0].orientation_constraints[0].absolute_x_axis_tolerance = goal_tolerance_[3];
  req.goal_constraints[0].orientation_constraints[0].absolute_y_axis_tolerance = goal_tolerance_[4];
  req.goal_constraints[0].orientation_constraints[0].absolute_z_axis_tolerance = goal_tolerance_[5];
  req.goal_constraints[0].orientation_constraints[0].weight = 1.0;


}

void BenchmarkManipulationTests::fillDualArmPlanningRequest(RobotPose &start_state, std::string name, moveit_msgs::MotionPlanRequest &req)
{
  tf::Quaternion q;
  req.group_name = group_name_;
  req.num_planning_attempts = 1;
  req.allowed_planning_time = ros::Duration(60.0);
  req.planner_id = planner_id_;

  // start state
  for(size_t i = 0; i < start_state.rangles.size(); ++i)
    req.start_state.joint_state.position.push_back(start_state.rangles[i]);
  for(size_t i = 0; i < start_state.langles.size(); ++i)
    req.start_state.joint_state.position.push_back(start_state.langles[i]);

  req.start_state.joint_state.name.push_back("r_shoulder_pan_joint");
  req.start_state.joint_state.name.push_back("r_shoulder_lift_joint");
  req.start_state.joint_state.name.push_back("r_upper_arm_roll_joint");
  req.start_state.joint_state.name.push_back("r_elbow_flex_joint");
  req.start_state.joint_state.name.push_back("r_forearm_roll_joint");
  req.start_state.joint_state.name.push_back("r_wrist_flex_joint");
  req.start_state.joint_state.name.push_back("r_wrist_roll_joint");
  req.start_state.joint_state.name.push_back("l_shoulder_pan_joint");
  req.start_state.joint_state.name.push_back("l_shoulder_lift_joint");
  req.start_state.joint_state.name.push_back("l_upper_arm_roll_joint");
  req.start_state.joint_state.name.push_back("l_elbow_flex_joint");
  req.start_state.joint_state.name.push_back("l_forearm_roll_joint");
  req.start_state.joint_state.name.push_back("l_wrist_flex_joint");
  req.start_state.joint_state.name.push_back("l_wrist_roll_joint");

  req.start_state.joint_state.position.push_back(start_state.body.z);
  req.start_state.joint_state.name.push_back("torso_lift_link");


  ROS_INFO("[exp] Start state %d angles %d joint names", int(req.start_state.joint_state.position.size()), int(req.start_state.joint_state.name.size()));
  // goal pose
  req.goal_constraints.resize(1);
  req.goal_constraints[0].position_constraints.resize(1);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.header.stamp = ros::Time::now();
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.header.frame_id = "odom";

  req.goal_constraints[0].position_constraints[0].link_name = "two_arms_object";
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.x = loc_map_[exp_map_[name].goal].at(0);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.y = loc_map_[exp_map_[name].goal].at(1);
  req.goal_constraints[0].position_constraints[0].constraint_region_pose.pose.position.z = loc_map_[exp_map_[name].goal].at(2);

  req.goal_constraints[0].position_constraints[0].constraint_region_shape.type = moveit_msgs::Shape::BOX;
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[0]);
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[1]);
  req.goal_constraints[0].position_constraints[0].constraint_region_shape.dimensions.push_back(goal_tolerance_[2]);
  req.goal_constraints[0].position_constraints[0].weight = 1.0;

  req.goal_constraints[0].orientation_constraints.resize(1);
  req.goal_constraints[0].orientation_constraints[0].orientation.header.stamp = ros::Time::now();
  req.goal_constraints[0].orientation_constraints[0].orientation.header.frame_id = "odom";
  req.goal_constraints[0].orientation_constraints[0].link_name = "two_arms_object";
  q.setRPY(loc_map_[exp_map_[name].goal].at(3), loc_map_[exp_map_[name].goal].at(4), loc_map_[exp_map_[name].goal].at(5));
  tf::quaternionTFToMsg(q, req.goal_constraints[0].orientation_constraints[0].orientation.quaternion);
  req.goal_constraints[0].orientation_constraints[0].absolute_x_axis_tolerance = goal_tolerance_[3];
  req.goal_constraints[0].orientation_constraints[0].absolute_y_axis_tolerance = goal_tolerance_[4];
  req.goal_constraints[0].orientation_constraints[0].absolute_z_axis_tolerance = goal_tolerance_[5];
  req.goal_constraints[0].orientation_constraints[0].weight = 1.0;

  // path constraint
  req.path_constraints.orientation_constraints.resize(1);
  req.path_constraints.orientation_constraints[0].orientation.header.stamp = ros::Time::now();
  req.path_constraints.orientation_constraints[0].orientation.header.frame_id = "odom";
  req.path_constraints.orientation_constraints[0].link_name = "two_arms_object";
  req.path_constraints.orientation_constraints[0].orientation.quaternion.x = 0;
  req.path_constraints.orientation_constraints[0].orientation.quaternion.y = 0;
  req.path_constraints.orientation_constraints[0].orientation.quaternion.z = 0;
  req.path_constraints.orientation_constraints[0].orientation.quaternion.w = 1;
  req.path_constraints.orientation_constraints[0].absolute_x_axis_tolerance = 0.04;
  req.path_constraints.orientation_constraints[0].absolute_y_axis_tolerance = 0.04;
  req.path_constraints.orientation_constraints[0].absolute_z_axis_tolerance = 3.14;

  req.start_state.multi_dof_joint_state.poses.resize(2);
  req.start_state.multi_dof_joint_state.frame_ids.resize(2);
  req.start_state.multi_dof_joint_state.child_frame_ids.resize(2);
  req.start_state.multi_dof_joint_state.frame_ids[0] = "two_arms_object";
  req.start_state.multi_dof_joint_state.frame_ids[1] = "two_arms_object";
  req.start_state.multi_dof_joint_state.child_frame_ids[0] = "r_wrist_roll_link";
  req.start_state.multi_dof_joint_state.child_frame_ids[1] = "l_wrist_roll_link";
  req.start_state.multi_dof_joint_state.poses[0] = rarm_object_pose_;
  req.start_state.multi_dof_joint_state.poses[1] = larm_object_pose_;
}

bool BenchmarkManipulationTests::setRobotPoseFromTrajectory(moveit_msgs::RobotTrajectory &trajectory, moveit_msgs::RobotState &trajectory_start, RobotPose &pose)
{
  // update the current pose with the previous start state of the robot
  getRobotPoseFromRobotState(trajectory_start, pose);

  if(experiment_type_ == 1)
  {
    if(!getJointPositionsFromTrajectory(trajectory.joint_trajectory, rjoint_names_, trajectory.joint_trajectory.points.size()-1, pose.rangles))
      return false;
  }
  else
  {
    if(!getJointPositionsFromTrajectory(trajectory.joint_trajectory, rjoint_names_, trajectory.joint_trajectory.points.size()-1, pose.rangles))
      return false;
    if(!getJointPositionsFromTrajectory(trajectory.joint_trajectory, ljoint_names_, trajectory.joint_trajectory.points.size()-1, pose.langles))
      return false;
  }
  return false;
}

bool BenchmarkManipulationTests::getJointPositionsFromTrajectory(const trajectory_msgs::JointTrajectory &traj, std::vector<std::string> &names, int waypoint, std::vector<double> &angles)
{
  unsigned int ind = 0;
  angles.resize(names.size());

  for(size_t i = 0; i < traj.joint_names.size(); i++)
  {
    if(names[ind].compare(traj.joint_names[i]) == 0)
    {
      angles[ind] = traj.points[waypoint].positions[i];
      ind++;
    }
    if(ind == names.size())
      break;
  }
  if(ind != names.size())
  {
    ROS_WARN("[exp] Not all of the expected joints were found in the trajectory.");
    return false;
  }
  return true;
}

bool BenchmarkManipulationTests::getRobotPoseFromRobotState(const moveit_msgs::RobotState &state, RobotPose &pose)
{
  unsigned int lind = 0, rind = 0;
  pose.langles.resize(ljoint_names_.size());
  pose.rangles.resize(rjoint_names_.size());

  // arms
  for(size_t i = 0; i < state.joint_state.name.size(); i++)
  {
    if(rind < rjoint_names_.size())
    {
      if(rjoint_names_[rind].compare(state.joint_state.name[i]) == 0)
      {
        ROS_DEBUG("[exp] [right-start] %-20s: %0.3f", rjoint_names_[rind].c_str(), state.joint_state.position[i]);
        pose.rangles[rind] = state.joint_state.position[i];
        rind++;
      }
    }
    if(lind < ljoint_names_.size())
    {
      if(ljoint_names_[lind].compare(state.joint_state.name[i]) == 0)
      {
        ROS_DEBUG("[exp] [left-start] %-20s: %0.3f", ljoint_names_[lind].c_str(), state.joint_state.position[i]);
        pose.langles[lind] = state.joint_state.position[i];
        lind++;
      }
    }
    if(rind == rjoint_names_.size() && lind == ljoint_names_.size())
      break;
  }
  if(rind != rjoint_names_.size() || lind != ljoint_names_.size())
  {
    ROS_WARN("[exp] Not all of the expected joints were assigned a starting position.");
    return false;
  }

  // torso
  for(size_t i = 0; i < state.joint_state.name.size(); i++)
  {
    if(state.joint_state.name[i].compare(spine_frame_) == 0)
    {
      pose.body.z = state.joint_state.position[i];
      break;
    }
  }

  return true;
}

bool BenchmarkManipulationTests::getBasePoseFromPlanningScene(const moveit_msgs::PlanningScene &scene, BodyPose &body)
{
  // base - if there is a map frame
  if(!scene.fixed_frame_transforms.empty())
  {
    for(size_t i = 0; i < scene.fixed_frame_transforms.size(); ++i)
    {
      if(scene.fixed_frame_transforms[i].header.frame_id.compare(world_frame_) == 0 && 
          scene.fixed_frame_transforms[i].child_frame_id.compare(robot_model_root_frame_) == 0)
      {
        body.x = scene.fixed_frame_transforms[i].transform.translation.x;
        body.y = scene.fixed_frame_transforms[i].transform.translation.y;
        body.theta =  2 * atan2(scene.fixed_frame_transforms[i].transform.rotation.z, scene.fixed_frame_transforms[i].transform.rotation.w);
        ROS_WARN("WARNING: The formula to compute the yaw of the base pose is untested.");
        break;
      }
    }
  }
  return true;
}

void BenchmarkManipulationTests::printPoseMsg(const geometry_msgs::Pose &p, std::string text)
{
  ROS_INFO("[%s] xyz: %0.3f %0.3f %0.3f  quat: %0.3f %0.3f %0.3f %0.3f", text.c_str(), p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
}
