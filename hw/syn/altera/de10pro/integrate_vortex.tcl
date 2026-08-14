package require -exact qsys 19.2

set required_instances {
    DUT
    BAR_INTERPRETER
    ddr4_ingress_pipe_ddr4a
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

    add_connection vortex_shell_0.avalon_master ddr4_ingress_pipe_ddr4a.s0
    set_connection_parameter_value vortex_shell_0.avalon_master/ddr4_ingress_pipe_ddr4a.s0 baseAddress 0x0
}

set expected_vortex_mem_connection \
    vortex_shell_0.avalon_master/ddr4_ingress_pipe_ddr4a.s0
set vortex_mem_connections [get_connections vortex_shell_0.avalon_master]
set vortex_mem_connection_count [llength $vortex_mem_connections]
set vortex_mem_connection_matches [string equal \
    [lindex $vortex_mem_connections 0] $expected_vortex_mem_connection]
if {$vortex_mem_connection_count != 1 || !$vortex_mem_connection_matches} {
    error "Vortex memory master must connect only through DDR4A ingress pipeline: $vortex_mem_connections"
}
set vortex_mem_base [get_connection_parameter_value \
    $expected_vortex_mem_connection baseAddress]
if {$vortex_mem_base != 0} {
    error "Vortex memory base address must be zero"
}

set expected_ingress_mem_connection \
    ddr4_ingress_pipe_ddr4a.m0/ddr4_clock_crossing_bridge_ddr4a.s0
set ingress_mem_connections [get_connections ddr4_ingress_pipe_ddr4a.m0]
set ingress_mem_connection_count [llength $ingress_mem_connections]
set ingress_mem_connection_matches [string equal \
    [lindex $ingress_mem_connections 0] $expected_ingress_mem_connection]
if {$ingress_mem_connection_count != 1 || !$ingress_mem_connection_matches} {
    error "DDR4A ingress pipeline must connect only to its clock-crossing bridge: $ingress_mem_connections"
}
set ingress_mem_base [get_connection_parameter_value \
    $expected_ingress_mem_connection baseAddress]
if {$ingress_mem_base != 0} {
    error "DDR4A ingress pipeline base address must be zero"
}

validate_system
save_system
