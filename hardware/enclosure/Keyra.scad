/* KEYRA REFINED — closed seam; compliant button centring; captured optical ring; 5 printed parts.
   Units mm. Fully parametric; no imported meshes needed for printed parts.
   IMPORTANT: this is not the narrower Espressif Micro-USB DevKitC-1.
   Evidence / uncertain dimensions are listed in Measurements.md.
   Export: openscad -o STL/Base.stl -D 'part="base"' Keyra.scad
   Board envelope parameters, not a substitute for first-print qualification.
*/
/* [View] */
part = "assembly"; // [assembly,exploded,base,lid,guide,button,light_ring,logo_colour,board]
print_orientation = true;
dongle = false; // false = cable; true = internal USB-C male/male coupler
press = 0; // 0..button_travel, assembly preview only
/* [Board: OEM outline + explicitly labelled component estimates] */
pcb_width = 27.94;
pcb_length = 57.15;
pcb_total_length = 63.39;
pcb_thickness = 1.6; // retained from physically fitting previous case; not OEM measured
pcb_bottom = 4.0;
boot_support_diameter = 4.8;
pcb_vertical_clearance = 0.10; // retaining-pad gap; nominal maximum PCB lift leaves 0.40 BOOT gap
board_clearance = 0.2; // clearance at locating rails, per side
boot_xy = [5.157,28.222]; // derived from OEM metric PDF vector circle
rst_xy = [5.157,33.475]; // OEM metric PDF vector circle
led_xy = [-5.151,18.377]; // OEM metric PDF vector circle
switch_height = 1.905; // model estimate above PCB; physically qualify before use
switch_actuator_diameter = 2.0; // OEM 2D circle, not a switch datasheet
switch_body_visual_height = 1.55; // visual partition only; not used for enclosure fit
switch_travel = 0.20; // engineering assumption for this unidentified switch
usb_centres = [-5.969,5.969]; // OEM Altium component origins
usb_height = 3.2; // conservative envelope, community model
usb_width = 9.0;
usb_z = 7.2; // previous fitting enclosure centre
native_usb_x = -5.969; // photographed cable/USB label, antenna points +Y
module_width = 18;
module_length = 25.5;
module_height = 3.2; // envelope above PCB, including shielding
headers_fitted = false; // photographs show no pin headers
header_above_pcb = 3.0; // assumed envelope; edit for your exact header
header_below_pcb = 8.0; // assumed envelope, not an OEM dimension
/* [Shell] */
wall = 1.6;
floor_thickness = 1.6;
roof_thickness = 1.6;
case_width = 32.64; // retain validated exterior; interior rails define 0.2 fit
case_rear = 70.2;
case_height = 18.2;
corner_radius = 6.0;
fit_clearance = 0.40; // stationary removable inserts, per side
snap_arm_clearance = 0.20; // keep a positive 0.20mm latch engagement, independent of insert clearance
snap_deflection = 0.40; // hook projection; nominal wall engagement 0.20
snap_arm_length = 10.5;
snap_arm_thickness = 1.4;
snap_arm_width = 6.0;
snap_root_height = 1.2;
snap_root_extra_width = 0.8;
snap_root_radius = 0.35;
snap_y = [6,62];
/* [USB bezel] */
native_aperture_width = 9.8;
native_aperture_height = 4.4;
native_outer_width = 11.8;
native_outer_height = 6.4;
native_outer_recess = 3.0; // cable overmould can enter near the socket, preserving mating depth
native_tunnel_end = -0.2; // 0.8 mm throat, clear of PCB front by 0.2 mm
usb_corner_radius = 1.0;
usb_bridge_face_depth = 0.8;
usb_bridge_face_overlap = 0.8;
/* [Dongle: explicit generic envelope, not a universal adapter standard] */
adapter_total_length = 27.6; // Delock 60035 published LxWxH: 27.6 x 10.7 x 6.0
adapter_housing_width = 10.7;
adapter_housing_height = 6.0;
adapter_male_length = 6.5; // assumption: verify against the selected coupler
adapter_insert_depth = 5.0; // assumption: not an OEM mating-depth specification
adapter_projection = 5.8;
adapter_bay_width = 14.4;
adapter_bay_height = 8.4;
adapter_front_lip = 0.6;
adapter_socket_mouth_y = -0.634; // community reference model
male_aperture_width = 9.0;
male_aperture_height = 3.4;
/* [Main button] */
button_diameter = 16.0;
button_flange_diameter = 18.0;
button_flange_thickness = 1.7;
button_xy = [0,boot_xy[1]];
button_rest_gap = 0.70; // owner requested a little more deliberate pre-travel; switch compression unchanged
post_contact_allowance = 0.00; // less than the user maximum 0.2
button_travel = button_rest_gap + switch_travel + post_contact_allowance;
button_radial_clearance = 0.40; // moving opaque guide, per side
optical_button_clearance = 0.60; // ring MUST NOT guide the button
optical_flange_clearance = 0.70;
manufacturing_surface_allowance = 0.15; // per mating surface; explicit sensitivity budget
button_entry_chamfer = 0.45;
button_shank_diameter = 10.0;
button_shank_flat_x = -4.0;
centring_angles = [45,135,225,315];
centring_leaf_thickness = 0.8;
centring_leaf_width = 2.4;
centring_leaf_preload = 0.10; // compliant contact, not a tight rigid bore
centring_leaf_tip_z = 10.55;
centring_leaf_root_z = 16.0;
centring_leaf_slot_clearance = 0.8;
centring_leaf_relief_radius = 3.5;
centring_leaf_outer_radius = 5.10;
centring_contact_height = 1.2;
button_socket_bore_diameter = 10.8;
button_socket_outer_diameter = 13.0;
button_socket_lip_projection = 0.15; // the lid also captures the cap; avoid a harsh one-time snap
button_socket_axial_clearance = 0.05;
button_protrusion = 0.50;
thumb_dish_depth = 0.40;
thumb_dish_diameter = 10.0; // support-free 10 mm bridge when printed face-down
contact_pad_diameter = 2.5;
contact_edge_radius = 0.20;
/* [Return spring] */
spring_thickness = 0.8; // flexure only; the protected rigid frame is 5.4mm thick
spring_arm_width = 2.2; // wider in-plane: firmer lateral guidance without narrowing the free-motion bore
spring_preload = 0.15;
spring_run_length = 20.0;
spring_root_x = 10.6;
spring_pad_x = 5.0;
spring_pad_y_offset = 2.5;
spring_root_fillet = 0.60;
spring_root_vertical_radius = 1.5; // tangent curved transition from thin flexure into thick fixed frame
spring_anchor_y_offset = 22.5;
spring_channel_clearance = 0.8;
carrier_diameter = 13.0;
carrier_stem_clearance = 0.50;
collector_relief_width = 1.6; // connects adjacent bores; removes an unprintable thin web
/* [Status light] */
ring_outer_diameter = 20.4;
ring_flange_diameter = 22.8;
ring_flange_thickness = 1.2; // 45 degree printable flange
ring_radial_clearance = 0.20;
ring_flange_clearance = 0.20;
ring_visible_height = 1.6;
light_collector_diameter = 6.4; // same visible ring, narrower clear feed clears the flexures
led_height = 1.7; // envelope / previous CAD estimate, not measured
led_light_gap = 1.00;
light_baffle_wall = 0.8;
collector_taper_height = 4.2;
/* [The Contour — exact open SVG path, never mirrored] */
logo_height = 14.0; // height of original centreline, 60 grid units
logo_stroke = 1.0;
logo_depth = 0.50;
logo_colour_support_margin = 0.40; // buried shoulders support the first black layer; visible stroke stays exact
logo_colour_thickness = 0.40; // embedded colour volume; only the optical ring prints separately
logo_xy = [0,53.0];
logo_curve_steps = 40;
/* [Quality] */
$fn = 80;
eps = 0.01;

/* [Final assembly / load path] */
adapter_y_tip_in=adapter_socket_mouth_y+adapter_insert_depth;
adapter_y_tip_out=adapter_y_tip_in-adapter_total_length;
adapter_y_back=adapter_y_tip_in-adapter_male_length;
adapter_y_front=adapter_y_tip_out+adapter_male_length;
case_front = dongle ? adapter_y_tip_out+adapter_projection : -4.0;
collar_width = dongle ? adapter_bay_width : native_aperture_width;
shell_seam_z = 10.6;
edge_softening = 2.0; // curved exterior arc R2, tangent to vertical; maximum overhang 45 degrees
edge_arc_steps = 12;
joint_clearance = 0.35;
seam_gap = 0.0;
retainer_socket_depth_clearance = 0.35;
usb_socket_depth_clearance = 0.30;
closure_design_layer_height = 0.10;
snap_pocket_side_clearance = 0.45;
snap_pocket_axial_clearance = 0.04;
guide_floor = 5.4;
stop_foundation_z = 13.6;
guide_base_z = 9.6;
guide_outer_diameter = 21.8;
guide_flange_flat_x = -7.0; // asymmetric D: cannot be installed backwards
anti_rotation_clearance = 0.40;
guide_hole_diameter = button_socket_outer_diameter+2*button_radial_clearance;
guide_ear_y_offset = 18.8;
guide_ear_length = 10.0;
guide_ear_outer_x = 14.72; // preserves at least 1.2mm of the outer shell around ear pockets
frame_locator_preload = 0.04; // stationary grip; low strain instead of a harsh interference fit
frame_locator_thickness = 0.8;
frame_locator_length = 4.0;
frame_locator_height = 3.0;
frame_locator_slot = 0.8;
frame_ear_y_clearance = 0.20;
frame_end_stop_gap = 0.20;
frame_end_stop_width = 10.0;
frame_end_stop_depth = 1.6;
retainer_width = 2.0;
retainer_lengths = [4.0,6.0];
retainer_x = 13.4;
retainer_y = [0.5,55.9];
stop_xy = [[-6.5,4.5],[6.5,4.5],[6.5,-4.5]];
stop_width = 3.0;
stop_length = 2.4;
stop_root_width = 5.0;
stop_root_length = 5.0;
frame_seat_clearance = 0.15;
ring_seat_clearance = 0.00; // broad annular seat touches the flange; ring cannot be pressed downward
collector_baffle_bottom = 9.6; // above tallest nominal board component 8.81

pcb_top=pcb_bottom+pcb_thickness;
H=case_height; PB=pcb_bottom; PT=pcb_top; header_lift=0;
L=case_rear-case_front; CY=(case_rear+case_front)/2;
roof_bottom=H-roof_thickness;
flange_top=roof_bottom;
flange_bottom=flange_top-button_flange_thickness;
spring_bottom=guide_base_z;
spring_top_free=spring_bottom+spring_thickness;
shank_bottom=spring_top_free-spring_preload;
tip_rest=PT+switch_height+button_rest_gap;
stop_top=flange_bottom-button_travel;
ring_bottom=roof_bottom-ring_flange_thickness;
ring_inner_lower=button_flange_diameter+2*optical_flange_clearance;
ring_inner_upper=button_diameter+2*optical_button_clearance;
socket_lip_bottom=centring_leaf_tip_z+centring_contact_height+spring_preload+button_socket_axial_clearance;
socket_top=socket_lip_bottom+2*button_socket_lip_projection;
snap_hook_outer_x=case_width/2-wall-snap_arm_clearance+snap_deflection;
snap_hook_start_x=case_width/2-wall-snap_arm_clearance-snap_arm_thickness/2;
snap_retaining_z=roof_bottom-snap_arm_length+1.2
 -(case_width/2-wall-snap_hook_start_x)*0.35/(snap_hook_outer_x-snap_hook_start_x);
snap_pocket_bottom_z=roof_bottom-snap_arm_length+0.15;
snap_pocket_ceiling_z=ceil((snap_retaining_z+snap_pocket_axial_clearance)/closure_design_layer_height)*closure_design_layer_height;
front_corner_clearance=(case_width/2-corner_radius<pcb_width/2 && case_front+corner_radius>0)
 ? corner_radius-wall-norm([pcb_width/2-(case_width/2-corner_radius),case_front+corner_radius])
 : min((case_width-2*wall-pcb_width)/2,-case_front-wall);
assert(front_corner_clearance>=board_clearance+0.01,"Increase front/side room or reduce corner radius");
assert((case_width-2*wall-pcb_width)/2>=board_clearance);
assert(case_rear-wall>=pcb_total_length+board_clearance);
assert(!headers_fitted,"This release targets the photographed board without headers");
assert(button_rest_gap>=0.3&&button_rest_gap<=0.8);
assert(button_rest_gap-pcb_vertical_clearance>=0.3);
assert(post_contact_allowance>=0&&post_contact_allowance<=0.2);
assert(logo_stroke*cos(180/$fn)>=0.8);
assert(logo_depth>=0.4&&logo_depth<=0.6);
assert(norm([72.3-63.8,31.37-27.04])*logo_height/60-logo_stroke-2*logo_colour_support_margin>0.2,"Keep buried logo shoulders open");
assert(roof_thickness-logo_depth-logo_colour_thickness>=0.6);
assert(shank_bottom-button_travel>PT+switch_height+0.5);
assert(collector_baffle_bottom>=PT+module_height+0.6);
assert(spring_thickness>=0.8&&spring_arm_width>=1.6);
assert(spring_channel_clearance>=2*manufacturing_surface_allowance+0.4);
assert(optical_button_clearance>=2*manufacturing_surface_allowance+0.25);
assert(button_radial_clearance>=2*manufacturing_surface_allowance+0.1-0.0001);
assert(centring_leaf_thickness>=0.8&&centring_leaf_width>=2.0);
assert(centring_leaf_tip_z>=shank_bottom+0.2);
assert(centring_leaf_root_z<flange_top-0.4);
assert(stop_top>=stop_foundation_z+0.3);
module box(w,l,h,x=0,y=0,z=0){translate([x-w/2,y-l/2,z])cube([w,l,h]);}
module disk(d,h,x=0,y=0,z=0){translate([x,y,z])cylinder(d=d,h=h);}
module rounded(w,l,h,r,x=0,y=0,z=0){translate([x,y,z])linear_extrude(h)offset(r=r)square([w-2*r,l-2*r],center=true);}
module ring(od,id,h,x=0,y=0,z=0){difference(){disk(od,h,x,y,z);disk(id,h+2*eps,x,y,z-eps);}}
module beam(a,b,w,h,z){translate([0,0,z])linear_extrude(h)hull(){translate(a)circle(d=w);translate(b)circle(d=w);}}
module capsule_cut(w,h,y0,depth,x=native_usb_x,z=usb_z+header_lift){
 translate([x,y0,z])rotate([-90,0,0])linear_extrude(depth)
 offset(r=usb_corner_radius)square([w-2*usb_corner_radius,h-2*usb_corner_radius],center=true);
}
module port_cut(x){
 if(dongle){
 capsule_cut(male_aperture_width,male_aperture_height,case_front-eps,native_tunnel_end-case_front+2*eps);
 box(adapter_bay_width,adapter_y_back-case_front-adapter_front_lip+0.4,adapter_bay_height,
 native_usb_x,(adapter_y_back+case_front+adapter_front_lip)/2+0.2,usb_z-adapter_bay_height/2);
 // Both original connector shells need internal space, even though UART remains externally closed.
 for(cx=usb_centres)translate([cx-native_usb_x,0,0])
 capsule_cut(native_aperture_width,native_aperture_height,adapter_y_back-0.4,
 native_tunnel_end-adapter_y_back+0.4+2*eps);
 }else{
 capsule_cut(native_aperture_width,native_aperture_height,case_front-eps,native_tunnel_end-case_front+2*eps);
 capsule_cut(native_outer_width,native_outer_height,case_front-eps,native_outer_recess+eps);
 }
}
module native_collar(z0,z1){intersection(){soft_outer();box(collar_width+2*wall,native_tunnel_end-case_front,z1-z0,native_usb_x,(native_tunnel_end+case_front)/2,z0);}}
function bz(p0,p1,p2,p3,t)=(1-t)*(1-t)*(1-t)*p0+3*(1-t)*(1-t)*t*p1+3*(1-t)*t*t*p2+t*t*t*p3;
function contour_points()=concat([[72.3,31.37],[75.5,33],[75.5,44]],
 [for(i=[1:logo_curve_steps])bz([75.5,44],[75.5,60],[64,73],[50,80],i/logo_curve_steps)],
 [for(i=[1:logo_curve_steps])bz([50,80],[36,73],[24.5,60],[24.5,44],i/logo_curve_steps)],
 [[24.5,33],[50,20],[63.8,27.04]]);
module contour(){
 p=contour_points();sc=logo_height/60;
 // SVG has downward Y; one coordinate conversion preserves top-right gap.
 for(i=[0:len(p)-2])hull(){
  translate([(p[i][0]-50)*sc,(50-p[i][1])*sc])circle(d=logo_stroke);
  translate([(p[i+1][0]-50)*sc,(50-p[i+1][1])*sc])circle(d=logo_stroke);
 }
}
module ring_cavity(){
 disk(ring_outer_diameter+2*ring_radial_clearance,roof_thickness+2*eps,button_xy[0],button_xy[1],roof_bottom-eps);
 translate([button_xy[0],button_xy[1],ring_bottom-eps])
 cylinder(d1=ring_flange_diameter+2*ring_flange_clearance+2*eps,
 d2=ring_outer_diameter+2*ring_flange_clearance-2*eps,h=ring_flange_thickness+2*eps);
 disk(light_collector_diameter+2*fit_clearance,roof_bottom-PT+eps,led_xy[0],led_xy[1],PT);
}
// Perimeter keeps the successful footprint. Insets only at exterior top/bottom edges.
function edge_inset(z)=edge_softening*(1-sqrt(1-pow((edge_softening*sin(45)-z)/edge_softening,2)));
function rim_points(inset,z)=let(r=corner_radius-inset,xx=case_width/2-corner_radius,yr=case_rear-corner_radius,yf=case_front+corner_radius)
 [for(q=[0:3])for(j=[0:20])let(a=q*90+j*90/20,cx=(q==0||q==3)?xx:-xx,cy=q<2?yr:yf)
 [cx+r*cos(a),cy+r*sin(a),z]];
module soft_outer(){
 h=edge_softening*sin(45);
 zs=concat([for(i=[0:edge_arc_steps])h*i/edge_arc_steps],
           [for(i=[edge_arc_steps:-1:0])H-h*i/edge_arc_steps]);
 n=84;
 points=[for(z=zs)each rim_points(edge_inset(min(z,H-z)),z)];
 faces=concat([[for(i=[n-1:-1:0])i]],
 [for(k=[0:len(zs)-2])for(i=[0:n-1])
  each [[k*n+i,k*n+(i+1)%n,(k+1)*n+(i+1)%n],
       [k*n+i,(k+1)*n+(i+1)%n,(k+1)*n+i]]],
 [[for(i=[0:n-1])(len(zs)-1)*n+i]]);
 polyhedron(points,faces,convexity=10);
}
module shell_slice(z0,z1){intersection(){soft_outer();box(case_width+1,L+1,z1-z0,0,CY,z0);}}
module base(){union(){difference(){union(){
 difference(){shell_slice(0,shell_seam_z-seam_gap);
 rounded(case_width-2*wall,L-2*wall,H,corner_radius-wall,0,CY,floor_thickness);}
 // Wide continuous edge seats; BOOT compression goes into the floor and these seats.
 for(s=[-1,1])box(1.4,50,PB-floor_thickness,s*14.02,31,floor_thickness);
 disk(boot_support_diameter,PB-floor_thickness,boot_xy[0],boot_xy[1],floor_thickness);
 for(s=[-1,1])for(y=[-0.7,pcb_length+0.7])
 rounded(2.4,1.0,PT+0.2-floor_thickness,0.4,s*13.45,y,floor_thickness);
 native_collar(floor_thickness,shell_seam_z);
 for(s=[-1,1])box((case_width/2-wall)-(pcb_width/2+board_clearance),41.5,1.8,
 s*((case_width/2-wall)+(pcb_width/2+board_clearance))/2,30.25,guide_base_z-1.8);

 }
 port_cut(native_usb_x);
 // Four wide frame ears lower into these open-top pockets.
 for(s=[-1,1])for(t=[-1,1])box(guide_ear_outer_x+joint_clearance-12.1,guide_ear_length+2*frame_ear_y_clearance,H,
 s*((guide_ear_outer_x+joint_clearance+12.1)/2),button_xy[1]+t*guide_ear_y_offset,guide_base_z);
 // Open admission above the USB mouth; the top shell supplies the bridge after PCB insertion.
 box(collar_width+2*wall+2*joint_clearance,
 native_tunnel_end-case_front+usb_socket_depth_clearance+eps,H,native_usb_x,(native_tunnel_end+case_front+usb_socket_depth_clearance)/2,usb_z);
 // Stepped face pocket: the upper bezel overlaps the deeper admission slot.
 box(collar_width+2*wall+4*joint_clearance+2*usb_bridge_face_overlap,
 usb_bridge_face_depth+usb_socket_depth_clearance+eps,H,native_usb_x,case_front+(usb_bridge_face_depth+usb_socket_depth_clearance)/2,usb_z);
 // Four robust lid cantilevers engage broad receiving grooves.
 for(s=[-1,1])for(y=snap_y)
 box(0.95,snap_arm_width+2*snap_pocket_side_clearance,snap_pocket_ceiling_z-snap_pocket_bottom_z,
 s*(case_width/2-wall+0.25),y,snap_pocket_bottom_z);
 // Closing rails enter vertically, above PCB. No 4 mm slide step.
 for(s=[-1,1])for(i=[0,1])
 box(retainer_width+2*joint_clearance,retainer_lengths[i]+2*joint_clearance,H,
 s*retainer_x,retainer_y[i],PT+pcb_vertical_clearance-retainer_socket_depth_clearance);
}
 if(dongle)intersection(){soft_outer();
 box(9.0,max(1,adapter_y_back-adapter_y_front-1.0),
 usb_z-adapter_housing_height/2-floor_thickness,native_usb_x,
 (adapter_y_back+adapter_y_front)/2,floor_thickness);}
}}
module d_shape(d,flat){intersection(){circle(d=d);translate([flat,-d])square([d,d*2]);}}
module d_disk(d,flat,h,z){translate([button_xy[0],button_xy[1],z])linear_extrude(h)d_shape(d,flat);}
module stem_clearance(){
 disk(carrier_diameter+2*spring_channel_clearance,0.9+eps,button_xy[0],button_xy[1],guide_base_z-eps);
 translate([button_xy[0],button_xy[1],guide_base_z+0.9])
 cylinder(d1=carrier_diameter+2*spring_channel_clearance,d2=guide_hole_diameter,h=(carrier_diameter+2*spring_channel_clearance-guide_hole_diameter)/2);
 disk(guide_hole_diameter,H,button_xy[0],button_xy[1],guide_base_z-eps);
 disk(contact_pad_diameter+2*carrier_stem_clearance,H,boot_xy[0],boot_xy[1],guide_base_z-eps);
}
module frame_locator(s,y,d=0){
 outer=guide_ear_outer_x;inner=outer-frame_locator_thickness;
 for(i=[0:15])hull(){for(j=[i,i+1])let(u=j/16,yy=y+4.3-frame_locator_length*u,dx=d*u*u*(3-2*u),tt=frame_locator_thickness+0.4*pow(1-u,3))
 box(tt,0.02,frame_locator_height,s*(outer-tt/2-dx),yy,guide_base_z);}
 // Full-height contact with 45-degree entry faces in either assembly direction.
 hull(){
 box(0.03,1.4,frame_locator_height,s*(outer-0.015-d),y+1.05,guide_base_z);
 box(joint_clearance+frame_locator_preload,1.4,frame_locator_height-1.0,
 s*(outer+(joint_clearance+frame_locator_preload)/2-d),y+1.05,guide_base_z+0.5);
 }
}
module guide_fixed(locator_deflection=0){union(){
 difference(){union(){
 rounded(28.5,2*spring_anchor_y_offset+4.0,guide_floor,2.0,0,button_xy[1],guide_base_z);
 for(s=[-1,1])for(t=[-1,1])box(guide_ear_outer_x-12.1,guide_ear_length,guide_floor,
 s*(guide_ear_outer_x+12.1)/2,button_xy[1]+t*guide_ear_y_offset,guide_base_z);
 disk(guide_outer_diameter,ring_bottom-ring_seat_clearance-guide_base_z,button_xy[0],button_xy[1],guide_base_z);
 }
 // Wide outer ribs are 5.4mm thick; the cap's D flange remains clear down to its stop plane.
 d_disk(button_flange_diameter+2*button_radial_clearance,guide_flange_flat_x-anti_rotation_clearance,H,stop_top-0.3);
 stem_clearance();
 for(s=[-1,1])for(t=[-1,1]){
  w=spring_arm_width+2*spring_channel_clearance;
  beam([s*spring_root_x,button_xy[1]+t*spring_pad_y_offset],
       [s*spring_root_x,button_xy[1]+t*(spring_anchor_y_offset-w/2-0.1)],w,2.3,guide_base_z-eps);
  beam([s*spring_root_x,button_xy[1]+t*spring_pad_y_offset],
       [s*spring_pad_x,button_xy[1]+t*spring_pad_y_offset],w,2.3,guide_base_z-eps);
 }
 disk(light_collector_diameter+2*fit_clearance+2*light_baffle_wall+0.8,H,led_xy[0],led_xy[1],guide_base_z-eps);
 beam(led_xy,button_xy,collector_relief_width,H,guide_base_z-eps);
 // Replace the outside strip with a resilient locator; its whole bottom still rests on the seat.
 for(s=[-1,1])for(t=[-1,1])let(y=button_xy[1]+t*guide_ear_y_offset)
 box(3.0,frame_locator_length,H,s*(guide_ear_outer_x-0.1),y+2.3,guide_base_z-eps);
 // The transverse relief opens the free end; retain the broad ear ends and roots.
 for(s=[-1,1])for(t=[-1,1])let(y=button_xy[1]+t*guide_ear_y_offset)
 box(3.0,frame_locator_slot,H,s*(guide_ear_outer_x-0.1),y+0.3-frame_locator_slot/2,guide_base_z-eps);
 for(s=[-1,1])for(y=snap_y)box(2.5,snap_arm_width+0.8,H,s*13.9,y,guide_base_z-eps);
 translate([10.8,49.5,guide_base_z+guide_floor-0.4])
 linear_extrude(0.5)polygon([[-1,0.8],[1,0.8],[0,-0.8]]);
 }
 // Three strong stops rest on the thick foundation; they never load the moving return arms.
 difference(){union(){for(p=stop_xy)hull(){
 rounded(stop_root_width,stop_root_length,eps,0.8,p[0],button_xy[1]+p[1],stop_foundation_z-eps);
 rounded(stop_width,stop_length,eps,0.6,p[0],button_xy[1]+p[1],stop_top-eps);}}
 stem_clearance();}
 for(s=[-1,1])for(t=[-1,1])frame_locator(s,button_xy[1]+t*guide_ear_y_offset,locator_deflection);
 }}
module flex_root(s,t,d=0){
 r=spring_root_vertical_radius;
 // Curved top transition; upright layers shrink inward, so no support is needed.
 pts=concat([[0,0],[r,0]],
 [for(i=[20:-1:0])let(u=r*i/20)
 [u,spring_thickness+r-sqrt(r*r-pow(r-u,2))]]);
 // Extrude the YZ profile across the full arm width.
 translate([s*spring_root_x-spring_arm_width/2,button_xy[1]+t*spring_anchor_y_offset,0])
 multmatrix([[0,0,1,0],[-t,0,0,0],[0,1,0,0],[0,0,0,1]])
 linear_extrude(spring_arm_width)polygon(pts);
}
module moving_carrier(deflection=0){difference(){
 union(){
 disk(carrier_diameter,spring_thickness,button_xy[0],button_xy[1],-deflection);
 // Captured cap and carrier travel as one: radial preload does not slide in the fixed frame.
 difference(){
 disk(button_socket_outer_diameter,socket_top-(guide_base_z+spring_thickness),button_xy[0],button_xy[1],spring_thickness-deflection-eps);
 d_disk(button_socket_bore_diameter,button_shank_flat_x-button_radial_clearance,H,spring_thickness-deflection-2*eps);
 }
 // The inward bead captures the four resilient cap feet; both ramps print at 45 degrees.
 translate([button_xy[0],button_xy[1],socket_lip_bottom-guide_base_z-deflection])rotate_extrude()
 // Bury the bead ends by 0.02mm: avoid coincident cylinder/revolve sliver faces.
 polygon([[button_socket_bore_diameter/2+0.02,0],
 [button_socket_bore_diameter/2-button_socket_lip_projection,button_socket_lip_projection],
 [button_socket_bore_diameter/2+0.02,2*button_socket_lip_projection],
 [button_socket_outer_diameter/2-0.02,2*button_socket_lip_projection],
 [button_socket_outer_diameter/2-0.02,0]]);
 }
 disk(contact_pad_diameter+2*carrier_stem_clearance,H,boot_xy[0],boot_xy[1],-deflection-eps);
 disk(light_collector_diameter+2*fit_clearance+2*light_baffle_wall+0.8,H,led_xy[0],led_xy[1],-deflection-eps);
 }}
module guide(deflection=0,locator_deflection=0){union(){guide_fixed(locator_deflection);
 translate([0,0,guide_base_z])union(){
  moving_carrier(deflection);
  for(s=[-1,1])for(t=[-1,1]){
   flex_root(s,t,deflection);
   start=[s*spring_root_x,button_xy[1]+t*spring_anchor_y_offset];
   bend=[s*spring_root_x,button_xy[1]+t*spring_pad_y_offset];
   pad=[s*spring_pad_x,button_xy[1]+t*spring_pad_y_offset];
   flex_beam(start,bend,spring_arm_width,spring_thickness,deflection,0,spring_run_length);
   flex_beam(bend,pad,spring_arm_width,spring_thickness,deflection,spring_run_length,spring_root_x-spring_pad_x);
   corner_fillet(bend[0]-s*spring_arm_width/2,bend[1]+t*spring_arm_width/2,-s,t,
    -spring_drop(spring_run_length/(spring_run_length+spring_root_x-spring_pad_x),deflection));
  }
 }
 }}
module snap_arm(s,y){
 x=s*(case_width/2-wall-snap_arm_clearance-snap_arm_thickness/2);
 box(snap_arm_thickness,snap_arm_width,snap_arm_length+3*eps,x,y,roof_bottom-snap_arm_length);
 translate([0,y-snap_arm_width/2,0])rotate([-90,0,0])linear_extrude(snap_arm_width)
 polygon([[x,-(roof_bottom-snap_arm_length)],
 [x+s*(snap_arm_thickness/2+snap_deflection),-(roof_bottom-snap_arm_length+0.35)],
 [x+s*(snap_arm_thickness/2+snap_deflection),-(roof_bottom-snap_arm_length+0.85)],
 [x,-(roof_bottom-snap_arm_length+1.2)]]);
 // A full-width shoulder overlaps the shaft and roof by volume, with no thin tip.
 rounded(snap_arm_thickness+snap_root_extra_width,snap_arm_width,snap_root_height+3*eps,snap_root_radius,
 x-s*0.25,y,roof_bottom-snap_root_height);

}
module lid_shell(){difference(){union(){
 difference(){shell_slice(shell_seam_z,H);
 rounded(case_width-2*wall,L-2*wall,roof_bottom-shell_seam_z+eps,corner_radius-wall,0,CY,shell_seam_z-eps);}
 native_collar(usb_z,roof_bottom);
 intersection(){soft_outer();box(collar_width+2*wall+2*joint_clearance+2*usb_bridge_face_overlap,
 usb_bridge_face_depth,roof_bottom-usb_z,native_usb_x,case_front+usb_bridge_face_depth/2,usb_z);}
 for(s=[-1,1])for(y=snap_y)snap_arm(s,y);
 // Two broad edge rails replace four tall, thin, fragile anti-lift pillars.
 for(s=[-1,1])for(i=[0,1]){
 rounded(retainer_width,retainer_lengths[i],roof_bottom-(PT+pcb_vertical_clearance),0.6,
 s*retainer_x,retainer_y[i],PT+pcb_vertical_clearance);
 intersection(){soft_outer();box(3.0,retainer_lengths[i],roof_bottom-shell_seam_z,s*13.9,retainer_y[i],shell_seam_z);}
 }

 // Light baffle stays at least 0.6 above the tallest reference component.
 // Broad end ribs bound frame Y motion without intruding below the shell seam.
 for(t=[-1,1])rounded(frame_end_stop_width,frame_end_stop_depth,
 roof_bottom-(shell_seam_z+0.2)+eps,0.4,0,
 button_xy[1]+t*(spring_anchor_y_offset+2+frame_end_stop_gap+frame_end_stop_depth/2),shell_seam_z+0.2);
 difference(){disk(light_collector_diameter+2*fit_clearance+2*light_baffle_wall,
 roof_bottom-collector_baffle_bottom,led_xy[0],led_xy[1],collector_baffle_bottom);
 disk(light_collector_diameter+2*fit_clearance,H,led_xy[0],led_xy[1],collector_baffle_bottom-eps);
 disk(ring_flange_diameter+2*fit_clearance,H,button_xy[0],button_xy[1]);}
 // Clamp the frame ears from above using the continuous shell, not small pillars.
 }
 ring_cavity();
 // Flat relief retains an opaque wall while clearing the left spring arm.
 box(4.0,8.0,roof_bottom-collector_baffle_bottom,-11.3,led_xy[1],collector_baffle_bottom-eps);
 for(s=[-1,1])for(t=[-1,1])box(guide_ear_outer_x+joint_clearance-12.1,guide_ear_length+2*frame_ear_y_clearance,
 guide_base_z+guide_floor-shell_seam_z+frame_seat_clearance,s*((guide_ear_outer_x+joint_clearance+12.1)/2),button_xy[1]+t*guide_ear_y_offset,shell_seam_z-eps);

 port_cut(native_usb_x);
 // Debossed open contour: black colour volume is fused into the lid, selectable in 3MF.
 translate([logo_xy[0],logo_xy[1],H-logo_depth])
 linear_extrude(logo_depth+eps)contour();
 translate([logo_xy[0],logo_xy[1],H-logo_depth-logo_colour_thickness])
 linear_extrude(logo_colour_thickness)offset(delta=logo_colour_support_margin)contour();
}}
module logo_colour(){translate([logo_xy[0],logo_xy[1],H-logo_depth-logo_colour_thickness])linear_extrude(logo_colour_thickness)offset(delta=logo_colour_support_margin)contour();}
module lid(){union(){lid_shell();logo_colour();}}
module centring_local(a){translate([button_xy[0],button_xy[1],0])rotate([0,0,a])children();}
module centring_leaf(deflection=0){
 // Constant-width radial leaf; smooth cubic shape is a kinematic surrogate for elastic checks.
 for(i=[0:15])hull(){for(j=[i,i+1])let(u=j/16,
 z=centring_leaf_root_z-(centring_leaf_root_z-centring_leaf_tip_z)*u,
 dx=deflection*u*u*(3-2*u))
 box(centring_leaf_thickness,centring_leaf_width,eps,
 centring_leaf_outer_radius-centring_leaf_thickness/2-dx,0,z);}
 // Rounded lead-in at the free end prevents catching the bearing's bottom edge.
 hull(){
 box(eps,1.6,eps,centring_leaf_outer_radius-deflection,0,centring_leaf_tip_z);
 disk(1.6,centring_contact_height/2,
 button_socket_bore_diameter/2+centring_leaf_preload-0.8-deflection,0,
 centring_leaf_tip_z+centring_contact_height/2);
 }
}
module button(centring_deflection=0){union(){difference(){union(){
 d_disk(button_flange_diameter,guide_flange_flat_x,button_flange_thickness,flange_bottom);
 disk(button_diameter,H+button_protrusion-flange_top-button_entry_chamfer,button_xy[0],button_xy[1],flange_top);
 translate([button_xy[0],button_xy[1],H+button_protrusion-button_entry_chamfer])
 cylinder(d1=button_diameter,d2=button_diameter-2*button_entry_chamfer,h=button_entry_chamfer);
 d_disk(button_shank_diameter,button_shank_flat_x,flange_bottom-shank_bottom+eps,shank_bottom);
 rounded_tip();
 }

 for(a=centring_angles)centring_local(a)
 box(6.5-centring_leaf_relief_radius,centring_leaf_width+2*centring_leaf_slot_clearance,
 centring_leaf_root_z-shank_bottom+eps,(6.5+centring_leaf_relief_radius)/2,0,shank_bottom-eps);

 r=(pow(thumb_dish_diameter/2,2)+pow(thumb_dish_depth,2))/(2*thumb_dish_depth);
 translate([button_xy[0],button_xy[1],H+button_protrusion+r-thumb_dish_depth])sphere(r=r,$fn=160);
}for(a=centring_angles)centring_local(a)centring_leaf(centring_deflection);
}}
module rounded_tip(){
 // 2.5 mm contact pad with a 0.2 mm rounded peripheral edge; flat centre 2.1 mm.
 translate([boot_xy[0],boot_xy[1],tip_rest])rotate_extrude()
 polygon(concat([[0,0],[contact_pad_diameter/2-contact_edge_radius,0]],
 [for(i=[0:8])[(contact_pad_diameter/2-contact_edge_radius)+contact_edge_radius*sin(i*90/8),contact_edge_radius*(1-cos(i*90/8))]],
 [[contact_pad_diameter/2,flange_bottom-tip_rest+eps],[0,flange_bottom-tip_rest+eps]]));
}
module light_ring(){difference(){union(){
 disk(ring_outer_diameter,ring_visible_height,button_xy[0],button_xy[1],roof_bottom);
 translate([button_xy[0],button_xy[1],ring_bottom])cylinder(d1=ring_flange_diameter,d2=ring_outer_diameter,h=ring_flange_thickness);
 // Collector meets the underside of the annulus directly, with no wires.
 dv=led_xy-button_xy;
 attach=button_xy+dv*((ring_inner_lower/2)/norm(dv));
 hull(){disk(2.0,eps,attach[0],attach[1],roof_bottom-eps);
 disk(light_collector_diameter,eps,led_xy[0],led_xy[1],roof_bottom-collector_taper_height);}
 disk(light_collector_diameter,roof_bottom-collector_taper_height-(PT+led_height+led_light_gap)+eps,led_xy[0],led_xy[1],PT+led_height+led_light_gap);
 }
 disk(ring_inner_upper,H+eps,button_xy[0],button_xy[1],roof_bottom-eps);
 disk(ring_inner_lower,roof_bottom+eps,button_xy[0],button_xy[1],0);
 // Wide lower clearance is exclusively optical; the opaque D guide sets the button alignment.
 // 45 degree lead-ins open both mouths, clear of the retaining shoulder at roof_bottom.
 translate([button_xy[0],button_xy[1],H-0.4])cylinder(d1=ring_inner_upper,d2=ring_inner_upper+0.8,h=0.41);
 translate([button_xy[0],button_xy[1],ring_bottom-eps])cylinder(d1=ring_inner_lower+0.8,d2=ring_inner_lower,h=0.41);
 }}
function spring_drop(u,d)=d*pow(min(u/0.95,1),2)*(3-2*min(u/0.95,1));
module flex_beam(a,b,w,h,d,prior,len){
 total=spring_run_length+spring_root_x-spring_pad_x;
 if(d==0)beam(a,b,w,h,0);else for(i=[0:9])hull(){
  translate([a[0]+(b[0]-a[0])*i/10,a[1]+(b[1]-a[1])*i/10,-spring_drop((prior+len*i/10)/total,d)])cylinder(d=w,h=h,$fn=12);
  translate([a[0]+(b[0]-a[0])*(i+1)/10,a[1]+(b[1]-a[1])*(i+1)/10,-spring_drop((prior+len*(i+1)/10)/total,d)])cylinder(d=w,h=h,$fn=12);
 }
}
module corner_fillet(cx,cy,sx,sy,z=0){
 // Fill the re-entrant corner with a genuine tangent quarter-circle fillet.
 difference(){
 box(spring_root_fillet,spring_root_fillet,spring_thickness,
 cx+sx*spring_root_fillet/2,cy+sy*spring_root_fillet/2,z);
 disk(2*spring_root_fillet,spring_thickness+2*eps,
 cx+sx*spring_root_fillet,cy+sy*spring_root_fillet,z-eps);
 }
}
module board(){
 color([0.06,0.10,0.08])box(pcb_width,pcb_length,pcb_thickness,0,pcb_length/2,PB);
 color("silver")box(module_width,19.5,module_height,0,48.9,PT);
 color([0.03,0.03,0.03])box(module_width,6.24,1,0,60.27,PT);
 for(x=usb_centres)color("silver")box(usb_width,7.6,usb_height,x,3.0,usb_z-usb_height/2+header_lift);
 color("gray")difference(){box(4.2,3.2,switch_body_visual_height,boot_xy[0],boot_xy[1],PT);
 disk(switch_actuator_diameter+0.1,switch_travel+0.02,boot_xy[0],boot_xy[1],PT+switch_body_visual_height-switch_travel-0.01);}
 color([0.15,0.16,0.18])disk(switch_actuator_diameter,switch_height-switch_body_visual_height,boot_xy[0],boot_xy[1],PT+switch_body_visual_height-min(max(press-button_rest_gap,0),switch_travel));
 color("gray")box(4.2,3.2,switch_height,rst_xy[0],rst_xy[1],PT);
 color("ivory")box(5,5,led_height,led_xy[0],led_xy[1],PT);
 if(headers_fitted)for(s=[-1,1])color("gold")box(2.54,55.89,header_above_pcb+pcb_thickness+header_below_pcb,s*12.7,28.575,PB-header_below_pcb);
}

module printed(which){
 if(which=="base")base();
 if(which=="lid")translate([0,0,H])rotate([180,0,0])lid();
 if(which=="lid_shell")translate([0,0,H])rotate([180,0,0])lid_shell();
 if(which=="logo_colour")translate([0,0,H])rotate([180,0,0])logo_colour();
 if(which=="guide")translate([0,0,-guide_base_z])guide();
 if(which=="button")translate([0,0,H+button_protrusion])rotate([180,0,0])button();

 if(which=="light_ring")translate([0,0,H])rotate([180,0,0])light_ring();
}
module assembled(explode=0){
 color("ivory")base();board();
 color("gray")translate([0,0,explode*0.3])guide(spring_preload+min(max(press,0),button_travel),frame_locator_preload);
 color("ivory")translate([0,0,explode*0.7-min(max(press,0),button_travel)])button(centring_leaf_preload);
 color([0.03,0.4,0.9,0.65])translate([0,0,explode])light_ring();
 color("ivory")translate([0,0,explode])lid_shell();
 color("black")translate([0,0,explode])logo_colour();
}
if(part=="assembly")assembled();
else if(part=="exploded")assembled(20);
else if(part=="board")board();
else if(print_orientation)printed(part);
else{
 if(part=="base")base();if(part=="lid")lid();if(part=="lid_shell")lid_shell();if(part=="logo_colour")logo_colour();
 if(part=="guide")guide(spring_preload+press,frame_locator_preload);if(part=="button")translate([0,0,-press])button(centring_leaf_preload);

 if(part=="light_ring")light_ring();
}
