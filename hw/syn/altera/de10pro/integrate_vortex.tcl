package require -exact qsys 19.2

set required_instances {
    DUT
    BAR_INTERPRETER
    ddr4_clock_crossing_bridge_ddr4a
}

foreach instance $required_instances {
    if {[lsearch -exact [get_instances] $instance] < 0} {
        error "required Qsys instance is missing: $instance"
    }
}

if {[lsearch -exact [get_instances] vortex_shell_0] < 0} {
    add_instance vortex_shell_0 vortex_shell 1.0
    set_instance_parameter_value vortex_shell_0 C_CTRL_ADDR_WIDTH 20
    set_instance_parameter_value vortex_shell_0 C_CTRL_DATA_WIDTH 32
    set_instance_parameter_value vortex_shell_0 C_MEM_ADDR_WIDTH 33
    set_instance_parameter_value vortex_shell_0 C_MEM_DATA_WIDTH 512
    set_instance_parameter_value vortex_shell_0 C_MEM_BURST_WIDTH 5

    add_connection DUT.coreclkout_hip vortex_shell_0.clock
    add_connection DUT.app_nreset_status vortex_shell_0.reset

    add_connection BAR_INTERPRETER.bri_master vortex_shell_0.ctrl
    set_connection_parameter_value BAR_INTERPRETER.bri_master/vortex_shell_0.ctrl baseAddress 0x1000

    add_connection vortex_shell_0.avalon_master ddr4_clock_crossing_bridge_ddr4a.s0
    set_connection_parameter_value vortex_shell_0.avalon_master/ddr4_clock_crossing_bridge_ddr4a.s0 baseAddress 0x0
}

validate_system
save_system
