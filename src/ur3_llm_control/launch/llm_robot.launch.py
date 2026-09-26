from os import environ, pathsep
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, SetEnvironmentVariable
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_prefix, get_package_share_directory


def generate_launch_description():
    share = get_package_share_directory("ur3_llm_control")
    prefix = get_package_prefix("ur3_llm_control")
    ur_description = FindPackageShare("ur_description")
    controller_file = share + "/config/ur_controllers.yaml"
    initial_positions = share + "/config/home_joints.yaml"
    robot_description = Command([
        FindExecutable(name="xacro"), " ",
        PathJoinSubstitution([ur_description, "urdf", "ur.urdf.xacro"]),
        " name:=ur ur_type:=ur3e prefix:=\"\" sim_gazebo:=true",
        " simulation_controllers:=", controller_file,
        " initial_positions_file:=", initial_positions,
    ])
    world = share + "/worlds/assignment2.world"
    return LaunchDescription([
        DeclareLaunchArgument("gazebo_gui", default_value="true"),
        DeclareLaunchArgument("launch_rviz", default_value="true"),
        DeclareLaunchArgument("start_task_manager", default_value="false"),
        SetEnvironmentVariable("GAZEBO_MODEL_PATH", share + "/models" + pathsep + environ.get("GAZEBO_MODEL_PATH", "")),
        SetEnvironmentVariable("GAZEBO_PLUGIN_PATH", prefix + "/lib" + pathsep + environ.get("GAZEBO_PLUGIN_PATH", "")),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                PathJoinSubstitution([FindPackageShare("gazebo_ros"), "launch", "gazebo.launch.py"])
            ),
            launch_arguments={"world": world, "gui": LaunchConfiguration("gazebo_gui")}.items(),
        ),
        Node(package="robot_state_publisher", executable="robot_state_publisher",
             parameters=[{"robot_description": robot_description, "use_sim_time": True}],
             output="screen"),
        Node(package="gazebo_ros", executable="spawn_entity.py",
             arguments=["-entity", "ur", "-topic", "robot_description", "-z", "0.74"],
             output="screen"),
        Node(package="controller_manager", executable="spawner",
             arguments=["joint_state_broadcaster", "-c", "/controller_manager"], output="screen"),
        Node(package="controller_manager", executable="spawner",
             arguments=["joint_trajectory_controller", "-c", "/controller_manager"], output="screen"),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                PathJoinSubstitution([FindPackageShare("ur_moveit_config"), "launch", "ur_moveit.launch.py"])
            ),
            launch_arguments={"ur_type": "ur3e", "use_sim_time": "true",
                              "launch_rviz": LaunchConfiguration("launch_rviz"),
                              "use_fake_hardware": "true"}.items(),
        ),
        Node(package="ur3_llm_control", executable="robot_skills", output="screen",
             parameters=[{"scene_file": share + "/config/scene.yaml", "home_file": initial_positions, "staging_file": share + "/config/staging_joints.yaml", "use_sim_time": True},
                         PathJoinSubstitution([FindPackageShare("ur_moveit_config"), "config", "kinematics.yaml"])]),
        Node(package="ur3_llm_control", executable="task_manager.py", output="screen",
             arguments=["--topic"], emulate_tty=True, condition=IfCondition(LaunchConfiguration("start_task_manager"))),
    ])
