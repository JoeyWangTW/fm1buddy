// fm1buddy cases: snap-fit, no screws, printed without supports (base: open side up; lid: top side down).
//
//   config   bare     the Feather RP2040 with USB Type A Host on its own (the USB host box)
//            oled     with the 128x64 OLED FeatherWing on top
//            sd       with the Adalogger FeatherWing (SD card) on top
//            sd_oled  Feather, Adalogger, OLED FeatherWing
//            cyd      the CYD (ESP32-2432S028R) in front, the Feather behind it
//            lite     ultra-light: a Waveshare RP2040-Zero, its USB-C the only port (a slot for a CYD's wires)
//   battery  room for a flat LiPo, and a hole for a small slide switch (not lite: its battery is a power bank)
//   part     base, lid, or preview (the case open, with stand-ins for the boards)
//
// openscad -D 'config="oled"' -D 'part="lid"' -o lid.stl fm1buddy-case.scad (tools/cases.sh makes them all)
//
// The board sizes are measured from Adafruit's CAD (Adafruit_CAD_Parts: 5723, 4650, 2922) and the CYD's
// drawing (ESP32-Cheap-Yellow-Display, Dimensions.png); the RP2040-Zero from Waveshare's (23.5 x 18). Feather coordinates: x along the board from the USB-C end,
// y across it from the 16-pin header's edge, z up from the board's underside.

/* [What] */
config = "oled";  // [bare, oled, sd, sd_oled, cyd, lite]
battery = false;
part = "preview";  // [preview, base, lid]
section = false;   // preview: cut in half, to see the inside

/* [Fit] */
wall = 2.0;
floor_t = 1.6;
lid_t = 1.6;
gap = 0.6;    // around the boards
fit = 0.25;   // between the lid's lip and the walls
lip_h = 4;

/* [Stack: measure yours] */
standoff = 3.0;    // under the Feather (its header pins stick out below)
wing_gap = 11.0;   // from the Feather's top to the wing's underside (male header 2.5 + female socket 8.5)
wing2_gap = 11.0;  // from the Adalogger's top to the OLED FeatherWing's underside
cyd_back = 7.0;    // how far the parts on the CYD's back stick out

/* [Battery] */
bat = [36, 29, 4.75];  // Adafruit 500 mAh (1578); 1200 mAh (258): [34, 62, 5]; 2000 mAh (2011): [60, 36, 7]
switch_hole = [9, 4.2];  // a mini slide switch (SS12D00), in line with the battery

$fn = 48;
LID_COLOR = "#ff8a3d";   // the installer page's orange
BASE_COLOR = "#36b37e";
PCB = 1.57;
eps = 0.01;

// ---------------------------------------------------------------------------------------------- boards ---
F = [50.8, 22.86];                                           // Feather, wings
F_HOLES = [[2.54, 2.54], [2.54, 20.32], [48.26, 2.54], [48.26, 20.32]];
F_USBC = [[-1.18, 6.96, 0.87], [6.12, 15.90, 4.77]];         // [min, max]
F_USBA = [[40.90, 4.86, 1.99], [54.28, 17.96, 8.49]];
F_JST = [[6.97, 14.85, PCB], [14.87, 22.45, 6.37]];
F_BOOT = [15.88, 5.08];
F_RESET = [9.53, 5.08];
OLED_GLASS = [[9.97, 2.93, PCB], [47.07, 19.93, 3.62]];
OLED_BTN = [[4.32, 6.99], [4.32, 11.43], [4.32, 15.88]];     // A, B, C (tops at 3.47)
OLED_BTN_TOP = 3.47;
OLED_UNDER = 2.96;                                           // its STEMMA QT, underneath
SD_SOCKET = [[0.70, 5.33, PCB], [19.20, 20.32, 5.57]];       // the card goes in at x = 0.7
SD_TOP = 5.57;
CYD = [86, 50];                                              // landscape: USB on the x = 86 edge, SD on y = 0
CYD_HOLES = [[4, 4], [82, 4], [4, 46], [82, 46]];
CYD_HOLE_D = 3.2;
CYD_SCREEN = [62, 47];                                       // the window: active area 57.6 x 43.2, centered
CYD_FRONT = 4.6;                                             // the display and touch panel, above its board

ZERO = [23.5, 18];                                           // RP2040-Zero: USB-C at x = 0
ZERO_PCB = 1.0;
ZERO_USB = [8.94, 3.26];                                     // its USB-C socket, centered on y; it overhangs a little

has_sd = config == "sd" || config == "sd_oled";
has_oled = config == "oled" || config == "sd_oled";

// ------------------------------------------------------------------------------------------- shapes ---
module rbox(p0, p1, r = 1.5) {
  s = p1 - p0;
  translate([p0.x + r, p0.y + r, p0.z]) linear_extrude(s.z) offset(r) square([s.x - 2 * r, s.y - 2 * r]);
}

module cube2(a, b) {  // the box between corners a and b
  translate(a) cube(b - a);
}

// a hole through a wall, centered on c (the wall's plane is given by axis: 0 = x wall, 1 = y wall)
module wall_hole(axis, c, w, h, r = 1) {
  t = wall + gap + 2;
  if (axis == 0) translate([c.x - t / 2, c.y - w / 2, c.z - h / 2]) rotate([90, 0, 90]) linear_extrude(t) offset(r) offset(-r) square([w, h]);
  else translate([c.x - w / 2, c.y + t / 2, c.z - h / 2]) rotate([90, 0, 0]) linear_extrude(t) offset(r) offset(-r) square([w, h]);
}

// a label engraved in a wall's outside: c is on that face; side is the face (y0, y1, x0, x1), so it reads from there
module wall_label(t, c, side, halign = "center", size = 3) {
  r = side == "y0" ? 0 : side == "y1" ? 180 : side == "x1" ? 90 : -90;
  translate(c) rotate([90, 0, r]) linear_extrude(1.2, center = true) text(t, size = size, halign = halign, valign = "center", font = "Liberation Sans:style=Bold");
}

// --------------------------------------------------------------------------------------- the box ---
// cavity [x0, y0] .. [x1, y1], height h (from the floor's top); a snap groove along both long sides
module tray(c0, c1, h) {
  difference() {
    rbox([c0.x - wall, c0.y - wall, -floor_t], [c1.x + wall, c1.y + wall, h], 3);
    rbox([c0.x, c0.y, 0], [c1.x, c1.y, h + 1], 1.5);
    for (y = [c0.y, c1.y]) translate([(c0.x + c1.x) / 2, y, h - lip_h / 2]) rotate([0, 90, 0]) cylinder(r = 0.7, h = (c1.x - c0.x) * 0.5, center = true);
  }
}

// the lid: a plate on the walls, a lip inside them with bumps that click into the grooves
module lid_plate(c0, c1) {
  rbox([c0.x - wall, c0.y - wall, 0], [c1.x + wall, c1.y + wall, lid_t], 3);
  difference() {
    rbox([c0.x + fit, c0.y + fit, -lip_h], [c1.x - fit, c1.y - fit, 0], 1.2);
    rbox([c0.x + fit + 1.2, c0.y + fit + 1.2, -lip_h - 1], [c1.x - fit - 1.2, c1.y - fit - 1.2, 1], 0.5);
  }
  for (y = [c0.y + fit, c1.y - fit]) translate([(c0.x + c1.x) / 2, y, -lip_h / 2]) rotate([0, 90, 0]) cylinder(r = 0.55, h = (c1.x - c0.x) * 0.45, center = true);
}

// posts with pegs, for a board's holes
module pegs(holes, h, d_post, d_peg, peg_h) {
  for (p = holes) translate([p.x, p.y, 0]) {
    cylinder(d = d_post, h = h);
    cylinder(d = d_peg, h = h + peg_h);
  }
}

// ------------------------------------------------------------------------------- the Feather boxes ---
so = battery ? bat.z + 1.2 : standoff;  // the Feather's underside
f_top = so + PCB;
sd_z = f_top + wing_gap;
oled_z = has_sd ? sd_z + PCB + wing2_gap : f_top + wing_gap;
top_z = has_oled ? oled_z : has_sd ? sd_z : so;               // the top board's underside
top_h = has_oled ? OLED_GLASS[1].z : has_sd ? SD_TOP : F_USBA[1].z;
inner_h = top_z + top_h + gap;
bat_c = [F.x / 2, F.y / 2];
fc0 = [min(-1.5, battery ? bat_c.x - bat.x / 2 - gap : 0), min(-gap, battery ? bat_c.y - bat.y / 2 - gap : 0)];
fc1 = [max(54.6, battery ? bat_c.x + bat.x / 2 + gap : 0), max(F.y + gap, battery ? bat_c.y + bat.y / 2 + gap : 0)];

module feather_base() {
  difference() {
    union() {
      tray(fc0, fc1, inner_h);
      pegs(F_HOLES, so, 5, 2.2, PCB + 0.5);
    }
    // USB-C (with room for the plug's body), USB-A
    wall_hole(0, [fc0.x - wall / 2, 11.43, so + 2.82], 13, 7.5, 2);
    wall_hole(0, [fc1.x + wall / 2, 11.41, so + 5.2], 17, 9.5, 1.5);
    // the SD card, and a notch to push it in with a fingernail
    if (has_sd) {
      wall_hole(0, [fc0.x - wall / 2, 12.8, sd_z + PCB + 2], 16, 4.6, 0.8);
      translate([fc0.x - wall - 1, 12.8, sd_z + PCB + 2]) rotate([0, 90, 0]) cylinder(d = 9, h = wall + 1.2);
    }
    if (battery && switch_hole.x > 0) {
      wall_hole(1, [F.x / 2, fc1.y + wall / 2, bat.z / 2 + 0.5], switch_hole.x, switch_hole.y, 0.5);
      wall_label("BATT", [F.x / 2, fc1.y + wall, bat.z + 3.5], "y1", size = 2.5);
    }
    // which port is which, on the front
    y0 = fc0.y - wall;
    wall_label("< POWER", [fc0.x + 1, y0, so + 3], "y0", "left");
    wall_label("HUB >", [fc1.x - 1, y0, so + 3], "y0", "right");
    if (has_sd) wall_label("< SD", [fc0.x + 1, y0, sd_z + PCB + 2], "y0", "left");
  }
}

// the OLED FeatherWing's buttons: a comb of tabs in the lid, hinged on the wall side, each with a nub under it
// that reaches its button. Tab width and the slots between them: the buttons are 4.45 mm apart
TAB_X0 = 0.6;  // the hinge
TAB_X1 = 6.8;  // the free end
TAB_W = 3.5;
TAB_T = 1.0;   // thinner than the lid, so it bends

module tab_cut() {
  y0 = OLED_BTN[0].y - TAB_W / 2 - 0.9;
  y1 = OLED_BTN[2].y + TAB_W / 2 + 0.9;
  difference() {
    translate([TAB_X0, y0, -1]) cube([TAB_X1 + 0.9 - TAB_X0, y1 - y0, lid_t + 2]);
    for (b = OLED_BTN) translate([TAB_X0 - 1, b.y - TAB_W / 2, -2]) cube([TAB_X1 - TAB_X0 + 1, TAB_W, lid_t + 4]);
  }
  for (b = OLED_BTN) translate([TAB_X0 + 1.2, b.y - TAB_W / 2 - 1, -1]) cube([TAB_X1 - TAB_X0, TAB_W + 2, 1 + lid_t - TAB_T]);
}

// up, OK, down, engraved next to the tabs. A is up: with the display upright, it's the top button (at -y)
module tab_marks() {
  x = TAB_X1 + 2.3;
  translate([x, OLED_BTN[0].y, 0]) polygon([[-1, 0.9], [1, 0.9], [0, -0.9]]);
  translate([x, OLED_BTN[1].y, 0]) circle(d = 1.8);
  translate([x, OLED_BTN[2].y, 0]) polygon([[-1, -0.9], [1, -0.9], [0, 0.9]]);
}

module feather_lid() {
  gap_btn = inner_h - (oled_z + OLED_BTN_TOP);  // from the lid's underside down to the buttons
  // posts that hold the stack down at the corners, where nothing is in the way
  posts = has_oled ? [F_HOLES[0], F_HOLES[1]] : has_sd ? [F_HOLES[0], F_HOLES[2], F_HOLES[3]] : F_HOLES;
  post_h = inner_h - top_z - PCB;
  difference() {
    union() {
      lid_plate(fc0, fc1);
      for (p = posts) translate([p.x, p.y, -post_h]) cylinder(d = 3.4, h = post_h + eps);
    }
    if (has_oled) {
      translate([OLED_GLASS[0].x + 0.8, OLED_GLASS[0].y + 0.8, -lip_h - 1]) cube([OLED_GLASS[1].x - OLED_GLASS[0].x - 1.6, OLED_GLASS[1].y - OLED_GLASS[0].y - 1.6, lip_h + lid_t + 2]);
      tab_cut();
      translate([0, 0, lid_t - 0.4]) linear_extrude(1) tab_marks();
    }
    if (config == "bare") {
      // BOOT and RESET, for a paperclip
      for (p = [F_BOOT, F_RESET]) translate([p.x, p.y, -lip_h - 1]) cylinder(d = 2.4, h = lip_h + lid_t + 2);
      // (the lid is seen from above, so its text reads the other way: mirrored, in the lid's coordinates)
      translate([F_BOOT.x + 2, F_BOOT.y, lid_t - 0.4]) linear_extrude(1) text("BOOT", size = 2, halign = "left", valign = "center", font = "Liberation Sans:style=Bold");
      translate([F_RESET.x - 2, F_RESET.y, lid_t - 0.4]) linear_extrude(1) text("RST", size = 2, halign = "right", valign = "center", font = "Liberation Sans:style=Bold");
    }
  }
  if (has_oled) for (b = OLED_BTN) translate([b.x, b.y, -(gap_btn - 0.15)]) cylinder(d = 1.8, h = gap_btn - 0.15 + (lid_t - TAB_T) + 0.3);
}

// ------------------------------------------------------------------------------------------ the CYD ---
// the CYD face up, on posts; the Feather across the box underneath it (USB-C on the y = 0 side, USB-A on the
// other); the battery beside the Feather
cyd_in = [CYD.x + 2 * gap, 54.6 + 1.5 + 2 * gap];  // wide enough for the Feather crosswise
cc0 = [-gap, -(cyd_in.y - CYD.y) / 2];
cc1 = [CYD.x + gap, CYD.y + (cyd_in.y - CYD.y) / 2];
cf_x = 10;                                    // where the Feather sits along the box
cf_so = standoff;
cyd_z = cf_so + F_USBA[1].z + 1 + cyd_back;   // the CYD board's underside
c_inner_h = cyd_z + PCB + CYD_FRONT;

// the Feather, turned to lie across the box: its x runs along the box's y
module cyd_feather_place() {
  translate([cf_x + F.y, cc0.y + 1.5 + gap, 0]) rotate([0, 0, 90]) children();
}

module cyd_base() {
  difference() {
    union() {
      tray(cc0, cc1, c_inner_h);
      pegs(CYD_HOLES, cyd_z, 6, 2.9, PCB + 0.5);
      cyd_feather_place() pegs(F_HOLES, cf_so, 5, 2.2, PCB + 0.5);
    }
    // the Feather's USB-C and USB-A, through the long sides
    cyd_feather_place() {
      wall_hole(0, [-1.5 - gap - wall / 2, 11.43, cf_so + 2.82], 13, 7.5, 2);
      wall_hole(0, [54.6 + gap + wall / 2, 11.41, cf_so + 5.2], 17, 9.5, 1.5);
    }
    // the CYD's USB (on its back, at the x = 86 end): wide enough for every revision of the board
    wall_hole(0, [cc1.x + wall / 2, 21, cyd_z - 2], 28, 7, 1.5);
    // its SD card (on its back, at y = 0)
    wall_hole(1, [48.4, cc0.y - wall / 2, cyd_z - 1.2], 16, 4, 0.8);
    if (battery && switch_hole.x > 0) {
      wall_hole(1, [60, cc1.y + wall / 2, 6], switch_hole.x, switch_hole.y, 0.5);
      wall_label("BATT", [60, cc1.y + wall, 11], "y1", size = 2.5);
    }
    // which port is which
    fx = cf_x + F.y - 11.43;  // the Feather's USB ports, along the box
    wall_label("POWER", [fx, cc0.y - wall, cf_so + 9], "y0");
    wall_label("HUB", [fx, cc1.y + wall, cf_so + 11.5], "y1");
    wall_label("SD", [48.4, cc0.y - wall, cyd_z - 5.5], "y0");
    wall_label("CYD USB", [cc1.x + wall, 21, cyd_z - 8], "x1", size = 2.5);
  }
}

module cyd_lid() {
  difference() {
    lid_plate(cc0, cc1);
    translate([CYD.x / 2 - CYD_SCREEN.x / 2, CYD.y / 2 - CYD_SCREEN.y / 2, -lip_h - 1]) linear_extrude(lip_h + lid_t + 2) offset(1) offset(-1) square(CYD_SCREEN);
  }
}

// ------------------------------------------------------------------------------------- ultra-light ---
// the RP2040-Zero on a low platform (so the OTG adapter's plug body clears the floor), its USB-C through the x = 0
// wall; the lid holds it down at the far corners and over the USB socket, and is thin over the board, so the RGB LED
// shows through. BOOT is under the lid (only needed to update the box's firmware)
z_so = 1.0;
z_top = z_so + ZERO_PCB;
z_usb = z_top + ZERO_USB.y / 2;  // the socket's center
lc0 = [-1.0, -0.4];
lc1 = [ZERO.x + 0.4, ZERO.y + 0.4];
l_inner_h = z_top + 5.6;

module lite_base() {
  difference() {
    union() {
      tray(lc0, lc1, l_inner_h);
      rbox([1, 1, -eps], [ZERO.x - 1, ZERO.y - 1, z_so], 0.5);
    }
    // the OTG adapter's plug (its body is bigger than the socket): open at the top, the lid closes it
    pb = z_usb - 3.5;
    wall_hole(0, [lc0.x - wall / 2, ZERO.y / 2, (pb + l_inner_h + 2) / 2], 12.5, l_inner_h + 2 - pb, 2);
    // the CYD's 4 wires (5V, GND, GP0, GP1): a slot in the far wall, closed by the lid
    wall_hole(0, [lc1.x + wall / 2, ZERO.y / 2, l_inner_h - 1], 6, 4, 0.8);
    wall_label("CYD", [lc1.x + wall, ZERO.y / 2, z_top], "x1", size = 2.5);
    wall_label("< HUB+POWER", [lc0.x + 0.5, lc0.y - wall, z_usb], "y0", "left", size = 2.2);
  }
}

module lite_lid() {
  difference() {
    lid_plate(lc0, lc1);
    // no lip at the USB end, where the socket and the soldered wires are
    translate([lc0.x - 1, lc0.y - 1, -lip_h - 1]) cube([10, lc1.y - lc0.y + 2, lip_h + 1]);
    // thin over the board: the LED shows through
    translate([2, 2, -1]) cube([ZERO.x - 4, ZERO.y - 4, 1 + lid_t - 0.6]);
  }
  down = l_inner_h - z_top;
  for (y = [1.5, ZERO.y - 1.5]) translate([ZERO.x - 1.5, y, -down]) cylinder(d = 2, h = down + eps);
  bar = l_inner_h - (z_top + ZERO_USB.y) - 0.1;
  translate([4, ZERO.y / 2 - 5, -bar]) cube([2, 10, bar + eps]);
}

// ---------------------------------------------------------------------------------------- previews ---
module stand_ins() {
  if (config == "lite") {
    translate([0, 0, z_so]) color("#222") cube([ZERO.x, ZERO.y, ZERO_PCB]);
    translate([-0.6, ZERO.y / 2 - ZERO_USB.x / 2, z_top]) color("#aaa") cube([7.3, ZERO_USB.x, ZERO_USB.y]);
  } else if (config == "cyd") {
    cyd_feather_place() translate([0, 0, cf_so]) color("#333") cube([F.x, F.y, PCB]);
    cyd_feather_place() color("#aaa") cube2([F_USBA[0].x, F_USBA[0].y, cf_so + F_USBA[0].z], [F_USBA[1].x, F_USBA[1].y, cf_so + F_USBA[1].z]);
    translate([0, 0, cyd_z]) color("#ffd400", 0.8) cube([CYD.x, CYD.y, PCB]);
    translate([(CYD.x - 69.2) / 2, 0, cyd_z + PCB]) color("#222") cube([69.2, CYD.y, CYD_FRONT - 0.2]);
    if (battery) translate([cf_x + F.y + 6, (cc0.y + cc1.y) / 2 - bat.y / 2, 0.5]) color("silver") cube(bat);
  } else {
    translate([0, 0, so]) color("#333") cube([F.x, F.y, PCB]);
    color("#aaa") cube2([F_USBA[0].x, F_USBA[0].y, so + F_USBA[0].z], [F_USBA[1].x, F_USBA[1].y, so + F_USBA[1].z]);
    color("#aaa") cube2([F_USBC[0].x, F_USBC[0].y, so + F_USBC[0].z], [F_USBC[1].x, F_USBC[1].y, so + F_USBC[1].z]);
    if (has_sd) {
      translate([0, 0, sd_z]) color("#333") cube([F.x, F.y, PCB]);
      color("#bbb") cube2(SD_SOCKET[0] + [0, 0, sd_z], SD_SOCKET[1] + [0, 0, sd_z]);
    }
    if (has_oled) {
      translate([0, 0, oled_z]) color("#333") cube([F.x, F.y, PCB]);
      color("#111") cube2(OLED_GLASS[0] + [0, 0, oled_z], OLED_GLASS[1] + [0, 0, oled_z]);
      for (b = OLED_BTN) translate([b.x - 2.3, b.y - 1.4, oled_z + PCB]) color("#bbb") cube([4.6, 2.8, OLED_BTN_TOP - PCB]);
    }
    if (battery) translate([bat_c.x - bat.x / 2, bat_c.y - bat.y / 2, 0.3]) color("silver") cube(bat);
  }
}

// --------------------------------------------------------------------------------------------- make ---
module cut() {
  if (section) intersection() {
    children();
    translate([-200, config == "cyd" ? 25 : config == "lite" ? ZERO.y / 2 : 11.43, -200]) cube(400);
  }
  else children();
}

cut() if (config == "lite") {
  if (part == "base") lite_base();
  if (part == "lid") translate([0, 0, lid_t]) mirror([0, 0, 1]) lite_lid();
  if (part == "preview") {
    color(BASE_COLOR) lite_base();
    stand_ins();
    translate([0, 0, l_inner_h + 12]) color(LID_COLOR) lite_lid();
  }
} else if (config == "cyd") {
  if (part == "base") cyd_base();
  if (part == "lid") translate([0, 0, lid_t]) mirror([0, 0, 1]) cyd_lid();
  if (part == "preview") {
    color(BASE_COLOR) cyd_base();
    stand_ins();
    translate([0, 0, c_inner_h + 25]) color(LID_COLOR) cyd_lid();
  }
} else {
  if (part == "base") feather_base();
  if (part == "lid") translate([0, 0, lid_t]) mirror([0, 0, 1]) feather_lid();
  if (part == "preview") {
    color(BASE_COLOR) feather_base();
    stand_ins();
    translate([0, 0, inner_h + 20]) color(LID_COLOR) feather_lid();
  }
}
