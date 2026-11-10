// ZoneFlow enclosure: parameters that belong to the enclosure alone.
//
// Everything about the board (outline, part positions, heights, the z levels
// of the front) comes from board_params.scad, which is generated from
// hardware/mechanical.yaml. Change board facts there, enclosure facts here.
// All dimensions in millimetres.

// ---- Walls and overall shape ------------------------------------------
wall        = 2.0;   // side walls, 5 perimeters at 0.4 mm
back_t      = 1.6;   // back wall; it is half the base's plastic, so kept thin
skin        = z_fascia_outer - z_fascia_inner;   // 2.0, front faces
pcb_gap     = 1.1;   // board edge to the inside of the side walls
corner_r    = 8;     // vertical outer corners
chamfer     = 2.0;   // 45 degree edge around the front faces
back_chamfer = 0.8;  // edge of the back, also hides elephant foot
reveal      = 0.6;   // shadow gap between base and front parts
reveal_h    = 0.8;
z_split     = 6.0;   // top of the base walls, above the board's top surface
cover_proud = 2.5;   // wiring cover stands this far in front of the fascia
cover_overlap = 1.5; // and laps this far over the fascia's lower edge
part_wall   = 1.6;   // partition between electronics and wiring
part_gap    = 0.4;   // partition to the bottom board edge
bay_h       = 46.0;  // inside height of the wiring compartment

// ---- FDM clearances ---------------------------------------------------
fit         = 0.25;  // per side, sliding fit between two printed parts
comp_clear  = 0.4;   // per side, printed part around a bought component
display_fit = 0.4;   // per side, locating ribs around the display panel
display_gap = z_fascia_outer - 1.0 - z_panel_front;  // 0.5, lip to glass
hole_comp   = 0.2;   // added to the diameter of small vertical holes

// ---- Fasteners --------------------------------------------------------
// "insert": M3 heat-set inserts, 5.7 long. "selftap": M3 screws cut their
// own thread in a plain pilot hole.
fastener     = "insert";
insert_d     = 4.0;
insert_depth = 6.7;
selftap_d    = 2.6;
selftap_depth = 9.0;
m3_clear     = 3.4;
m3_csk_d     = 6.4;   // countersunk head
boss_d       = 8.0;

// ---- Front lip (tongue of the front parts inside the base walls) -------
lip_t  = 1.6;
lip_z0 = 0.6;         // stays above the board, which it overhangs

// ---- Display bezel -----------------------------------------------------
bezel_lip   = 1.0;    // plastic left in front of the glass
bezel_cham  = 1.0;    // 45 degree chamfer of the window, widening outward
rib_t       = 1.2;
rib_z0      = 9.4;    // locating ribs come down to here

// ---- Display carrier ("bridge") ---------------------------------------
deck_margin  = 1.0;   // deck extends this far beyond the panel outline
leg_d        = 8.0;
foot_size    = 4.0;

// ---- Encoder and knob --------------------------------------------------
enc_hole_d   = 14.0;  // large so the fascia can swing shut over the shaft
knob_d       = 30;
knob_gap     = 2.0;   // knob underside above the fascia at rest
knob_roof    = 2.0;
knob_wall    = 2.4;
knob_bore_fit = 0.1;  // per side on the D shaft; push fit
knob_flutes  = 36;

// ---- Buttons -----------------------------------------------------------
btn_size     = [16, 8];   // pill, visible face
btn_proud    = 0.6;
btn_flange   = 2.0;       // flange beyond the pill, per side
btn_flange_z0 = 8.0;      // underside of the cap at rest
btn_flange_gap = 0.6;     // flange to fascia; cap never preloads the switch
btn_socket_d = 4.3;       // loose on the 3.5 mm actuator

// ---- Light pipes -------------------------------------------------------
lp_size      = 2.4;   // square section
lp_hole      = 2.9;   // square hole in the fascia
lp_bottom_z  = 1.5;   // 0.7 above the LED
lp_rail_z    = [8.2, 10.3];
lp_chan_wall = 1.0;
lp_fit       = 0.1;   // per side between rail and channel: press fit

// ---- Label and markings ------------------------------------------------
// Recess for a standard address label: 28 x 89 mm (Dymo 99010) or
// 29 x 90 mm (Brother DK-11201).
label_size   = [90.5, 29.5];
label_y0     = 4.5;
label_depth  = 0.4;
mark_depth   = 0.4;
mark_size    = 3.6;
mark_y       = 37.2;
mark_font    = "Liberation Sans:style=Bold";
wordmark     = "ZoneFlow";

// ---- Wall mounting -----------------------------------------------------
keyhole_x    = [19.5, 94.5];   // 75 mm apart, centred
keyhole_y    = 78;             // centre of the large opening
keyhole_slot = 8;              // the narrow slot runs this far up
keyhole_big  = 9.5;            // for screw heads up to 9 mm
keyhole_small = 4.6;           // for screws up to 4.5 mm (No. 8)
keyhole_head_h = 5.5;          // room for the head behind the back wall
lower_fix    = [57, -27];      // third screw, inside the wiring compartment

// ---- Wiring compartment -------------------------------------------------
// Bottom entries: x position and width. 12.5 takes 18 AWG multi-conductor
// sprinkler cable; 7 takes the transformer lead or a sensor cable.
entry        = [[12, 7], [36, 12.5], [78, 12.5], [102, 7]];
entry_web    = 0.5;            // knock-out skin left in each entry
rear_ko      = [[30, -25], [84, -25]];
rear_ko_d    = 22;
rear_ko_skin = 0.4;
tie_slot     = [4.0, 2.5];     // cable tie tunnel, width x height
cover_screw  = [57, -41];
hook_x       = [25.92, 74.03]; // fascia hooks, in the gaps between plugs
hook_w       = 3.0;
toe_x        = [8.0, 109.25];  // cover toes, above USB and Qwiic
toe_w        = 6.0;

// ---- Fascia screws (through the top wall of the base) -------------------
top_screw_x  = [20, 70];       // both more than 15 mm from the antenna
top_screw_z  = 2.4;

// ---- Ventilation -------------------------------------------------------
louvre_gap   = 1.4;   // measured square to the slit: the insect limit
louvre_pitch = 4.5;
louvre_y     = [12, 98];
louvre_z     = [-18.5, -2.5];

// ---- Colours for renders -----------------------------------------------
col_base   = "#d8d6d0";
col_front  = "#33383d";
col_knob   = "#d8d6d0";
col_btn    = "#d8d6d0";
col_pipe   = "#b4e08c";
col_bridge = "#8a9097";
col_pcb    = "#1f6b45";
col_part   = "#4a4a4a";
col_mate   = "#3f8f4f";
col_glass  = "#0c0f14";

$fn = 64;
