#include <math.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <linux/input.h>

#include "xwiigun.h"

#define MAX_TRACKING_DISTANCE 50.0f

// manual adjustment based on single Wii remote, needs to be configurable
#define ACCEL_CORR 20
#define ACCEL_IMPACT 0.7f
#define ACCEL_SMOOTH 0.5f

//#define M_PI 3.1415926535f

void reset_ir(struct xwii_event_abs ir[4])
{
    for (int i = 0; i < 4; i++) {
        ir[i].x = ir[i].y = 1023;
        ir[i].z = 0;
    }
}

static inline bool xwii_event_ir_is_valid(const struct xwii_event_abs *abs)
{
	return abs->x != 1023 || abs->y != 1023;
}

static double distance(const struct xwii_event_abs *a, const struct xwii_event_abs *b)
{
    //assert(xwii_event_ir_is_valid(a) && xwii_event_ir_is_valid(b));
    return sqrt(pow(a->x - b->x, 2) + pow(a->y - b->y, 2));
}

static double distance_to_line(const struct xwii_event_abs *a, const struct xwii_event_abs *b, const struct xwii_event_abs *p)
{
    return distance(a, p) * sin(atan2(p->y - a->y, p->x - a->x) - atan2(b->y - a->y, b->x - a->x));
}

static inline double todeg(double r)
{
    return r * (180.0f / M_PI);
}

// snap to closest 45 degrees
static int deg45(double deg)
{
    if (deg >= 22.5 && deg < 67.5)
        return 45;
    if (deg >= 67.5 && deg < 112.5)
        return 90;
    if (deg >= 112.5 && deg < 157.5)
        return 135;
    if (deg >= 157.5 && deg < 202.5)
        return 180;
    if (deg >= 202.5 && deg < 247.5)
        return 225;
    if (deg >= 245.5 && deg < 292.5)
        return 270;
    if (deg >= 292.5 && deg < 337.5)
        return 315;

    return 0;
}

static double line_angle(struct xwii_event_abs *a, struct xwii_event_abs *b)
{
    return atan2(b->y - a->y, a->x - b->x);
}

static void rotate(struct xwii_event_abs *anchor, double angle, struct xwii_event_abs *p)
{
    double s = sin(angle);
    double c = cos(angle);

    p->x -= anchor->x;
    p->y -= anchor->y;

    double xnew = p->x * c - p->y * s;
    double ynew = p->x * s + p->y * c;

    p->x = xnew + anchor->x;
    p->y = ynew + anchor->y;
}

void xwiigun_manage_accelerators(struct xwiigun *gun, struct input_event *ev)
{
	if (gun->accel.x == 0 && gun->accel.y == 0 && gun->accel.z == 0) {
		memset(&gun->accel, 0, sizeof(gun->accel));
	} else {
		if ( ev->code == ABS_RX ) 
			gun->accel.x += (ev->value - gun->accel.x) * ACCEL_SMOOTH;
		else if ( ev->code == ABS_RY ) 
			gun->accel.y += (ev->value - gun->accel.y) * ACCEL_SMOOTH;
		else if ( ev->code == ABS_RZ )
			gun->accel.z += (ev->value - gun->accel.z) * ACCEL_SMOOTH;
	}
}

void xwiigun_manage_ir(struct xwiigun *gun, struct input_event *ev)
{
	switch (ev->code)
	{
		case ABS_HAT0X:
			gun->ir.points[0].x = ev->value;
		break;
		
		case ABS_HAT0Y:
			gun->ir.points[0].y = ev->value;
		break;
		
		case ABS_HAT1X:
			gun->ir.points[1].x = ev->value;
		break;
		
		case ABS_HAT1Y:
			gun->ir.points[1].y = ev->value;
		break;
		
		case ABS_HAT2X:
			gun->ir.points[2].x = ev->value;
		break;
		
		case ABS_HAT2Y:
			gun->ir.points[2].y = ev->value;
		break;
		
		case ABS_HAT3X:
			gun->ir.points[3].x = ev->value;
		break;
		
		case ABS_HAT3Y:
			gun->ir.points[3].y = ev->value;
		break;		
		//case SYN_REPORT:
		//ripulisco tutto
		//	memset(gun->ir.points, 0, sizeof(struct xwii_event_abs));
		//break;
	}
}

void xwiigun_compute_ir(struct xwiigun *gun)
{
    struct xwii_event_abs points[4];
    int npoints = 0, ntracking = 0;

    // detect all visible ir points, reset all visible points
    for (int i = 0; i < 4; i++) {
        //if (!xwii_event_ir_is_valid(&e->v.abs[i]) || (e->v.abs[i].x == 0 && e->v.abs[i].y == 0))
        //    continue;
		if (!xwii_event_ir_is_valid(&gun->ir.points[i]) || (&gun->ir.points[i].x == 0 && &gun->ir.points[i].y == 0))
            continue;
		
        struct xwii_event_abs *p = &points[npoints];

        p->x = gun->ir.points[i].x;
        p->y = 768 - gun->ir.points[i].y;
        p->z = 0; // non-tracking
		
        npoints++;
    }

    // reset current state
    reset_ir(gun->ir.now);

    // we need two points
    if (npoints < 2)
    {
	gun->offscreen = 1;
    	return;
    }

    // adjust tilt with accelerometer data
    for (int i = 0; i < npoints; i++) {
        rotate(&gun->center, ((gun->accel.x + ACCEL_CORR) * M_PI / 180) * ACCEL_IMPACT, &points[i]);
    }

    // setup tracking of visible points
    for (int i = 0; i < npoints; i++) {
        struct xwii_event_abs *p = &points[i];
        int tracking_side = SIDE_INVALID;
        double tracking_dist = MAX_TRACKING_DISTANCE;

        // find closest tracking distance
        for (int j = 0; j < 4; j++) {
            if (!xwii_event_ir_is_valid(&gun->ir.prev[j]))
                continue;

            double dist = distance(&gun->ir.prev[j], p);

            if (dist < tracking_dist) {
                tracking_side = j;
                tracking_dist = dist;
            }
        }

        // check if tracking is possible
        if (tracking_dist < MAX_TRACKING_DISTANCE) {
            p->z = tracking_side;
            ntracking++;
        }
    }


    // if we track all visible points we keep the state
    if (ntracking == npoints) {
        for (int i = 0; i < npoints; i++) {
            memcpy(&gun->ir.now[points[i].z], &points[i], sizeof(gun->ir.now[0]));
        }
    }
    // if we see two points, guess where they are
    else if (npoints == 2) {
        const static struct xwii_event_abs corners[4] = {
            { 0,    0,   0 }, // top left
            { 1024, 0,   0 }, // top right 
            { 1024, 768, 0 }, // bottom right 
            { 0,    768, 0 }  // bottom left
        };

        double ar = line_angle(&points[0], &points[1]);
        double ad = todeg(ar) + 180.0f;
        int d45 = deg45(ad);

        // top-left or bottom-right
        if (d45 == 45 || d45 == 225) {
            double dist_tl = distance(&points[0], &corners[0]) + distance(&points[1], &corners[0]);
            double dist_br = distance(&points[0], &corners[2]) + distance(&points[1], &corners[2]);

            if (dist_tl > dist_br) {
                if (d45 == 45) {
                    memcpy(&gun->ir.now[SIDE_TOP], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_LEFT], &points[0], sizeof(gun->ir.now[0]));
                } else {
                    memcpy(&gun->ir.now[SIDE_LEFT], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_TOP], &points[0], sizeof(gun->ir.now[0]));
                }
            } else {
                if (d45 == 45) {
                    memcpy(&gun->ir.now[SIDE_RIGHT], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_BOTTOM], &points[0], sizeof(gun->ir.now[0]));
                } else {
                    memcpy(&gun->ir.now[SIDE_BOTTOM], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_RIGHT], &points[0], sizeof(gun->ir.now[0]));
                }
            }
        }
        // top-right or bottom-left
        else if (d45 == 135 || d45 == 315) {
            double dist_tr = distance(&points[0], &corners[1]) + distance(&points[1], &corners[1]);
            double dist_bl = distance(&points[0], &corners[3]) + distance(&points[1], &corners[3]);

            if (dist_tr > dist_bl) {
                if (d45 == 135) {
                    memcpy(&gun->ir.now[SIDE_TOP], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_RIGHT], &points[0], sizeof(gun->ir.now[0]));
                } else {
                    memcpy(&gun->ir.now[SIDE_RIGHT], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_TOP], &points[0], sizeof(gun->ir.now[0]));
                }
            } else {
                if (d45 == 135) {
                    memcpy(&gun->ir.now[SIDE_LEFT], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_BOTTOM], &points[0], sizeof(gun->ir.now[0]));
                } else {
                    memcpy(&gun->ir.now[SIDE_BOTTOM], &points[1], sizeof(gun->ir.now[0]));
                    memcpy(&gun->ir.now[SIDE_LEFT], &points[0], sizeof(gun->ir.now[0]));
                }
            }
        }
        // FIXME: top-bottom and left-right
        else {
            return; // hack to ignore this frame because we have no data
        }

    }
    else if (npoints >= 3) {
        // find hypotenuse and its orientation, rest will come
        /*int angle = -1;
        int apoints[4];

        // find a line that's closest to being 180 or 90 degrees
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                if (j == i) continue;

                int d  = deg45(todeg(line_angle(&points[i], &points[j])) + 180);
                if (d % 90 == 0) {
                    angle = d;
                    apoints[0] = i;
                    apoints[1] = j;
                }
            }
        }

        // ugly third point hack
        for (int i = 0; i < 3; i++)
            if (i != apoints[0] && i != apoints[1])
                apoints[2] = i;

        // we definitely have top and bottom
        if (angle == 90) {
            memcpy(&gun->ir.now[SIDE_BOTTOM], &points[apoints[0]], sizeof(gun->ir.now[0]));
            memcpy(&gun->ir.now[SIDE_TOP], &points[apoints[1]], sizeof(gun->ir.now[0]));
        } else if (angle == 270) {
            memcpy(&gun->ir.now[SIDE_TOP], &points[apoints[0]], sizeof(gun->ir.now[0]));
            memcpy(&gun->ir.now[SIDE_BOTTOM], &points[apoints[1]], sizeof(gun->ir.now[0]));
        }

        // determine if remaining point is on the left or right
        if (angle == 90 || angle == 270) {
            // XXX: this is wrong
            if (points[apoints[2]].x < 512) {
                memcpy(&gun->ir.now[SIDE_LEFT], &points[apoints[2]], sizeof(gun->ir.now[0]));
            } else {
                memcpy(&gun->ir.now[SIDE_RIGHT], &points[apoints[2]], sizeof(gun->ir.now[0]));
            }
        }

        // we definitely have left and right
        if (angle == 0) {
            memcpy(&gun->ir.now[SIDE_LEFT], &points[apoints[0]], sizeof(gun->ir.now[0]));
            memcpy(&gun->ir.now[SIDE_RIGHT], &points[apoints[1]], sizeof(gun->ir.now[0]));
        } else if (angle == 180) {
            memcpy(&gun->ir.now[SIDE_RIGHT], &points[apoints[0]], sizeof(gun->ir.now[0]));
            memcpy(&gun->ir.now[SIDE_LEFT], &points[apoints[1]], sizeof(gun->ir.now[0]));
        }

        // determine if remaining point is on the top or bottom
        if (angle == 0 || angle == 180) {
            // XXX: this is wrong
            if (points[apoints[2]].y < 384) {
                memcpy(&gun->ir.now[SIDE_TOP], &points[apoints[2]], sizeof(gun->ir.now[0]));
            } else {
                memcpy(&gun->ir.now[SIDE_BOTTOM], &points[apoints[2]], sizeof(gun->ir.now[0]));
            }
        }

        if (npoints == 4) {
            // uglier fourth point hack
            for (int i = 0; i < 4; i++) {
                if (i != apoints[0] && i != apoints[1] && i != apoints[2])
                    apoints[3] = i;
            }

            // find what side is missing
            for (int i = 0; i < 4; i++) {
                if (!xwii_event_ir_is_valid(&gun->ir.now[i]))
                    memcpy(&gun->ir.now[i], &points[apoints[3]], sizeof(gun->ir.now[0]));
            }
        }*/
		
		//il top e' sempre il vertice con la y minore
		//il left e' sempre il vertice con la x minore
		//il bottom e' sempre il vertice con la y maggiore
		//il right e' sempre il vertice con la x maggiore
		
		int i_top = -1, i_right = -1, i_left = -1, i_bottom = -1;
		int x_min = 2000, x_max = 0;
		int y_min = 2000, y_max = 0;
		
		for ( int i = 0 ; i < npoints ; i++ )
		{
			if ( !xwii_event_ir_is_valid(&points[i]) ) continue;
			
			if ( points[i].x < x_min ) 
			{
				x_min = points[i].x;
				i_left = i;
			}
			
			if ( points[i].x > x_max )
			{
				x_max = points[i].x;
				i_right = i;
			}
			
			if ( points[i].y < y_min )
			{
				y_min = points[i].y;
				i_top = i;
			}
			
			if ( points[i].y > y_max )
			{
				y_max = points[i].y;
				i_bottom = i;
			}
		}
		
		if ( i_top >= 0 ) memcpy(&gun->ir.now[SIDE_TOP], &points[i_top], sizeof(gun->ir.now[0]));
		if ( i_left >= 0 ) memcpy(&gun->ir.now[SIDE_LEFT], &points[i_left], sizeof(gun->ir.now[0]));
		if ( i_right >= 0 ) memcpy(&gun->ir.now[SIDE_RIGHT], &points[i_right], sizeof(gun->ir.now[0]));
		if ( i_bottom >= 0 ) memcpy(&gun->ir.now[SIDE_BOTTOM], &points[i_bottom], sizeof(gun->ir.now[0]));
    }

    // three point code already calculates everything
    if (npoints == 4) {
        // set calibration when all points visible
        memcpy(&gun->ir.cal, &gun->ir.now, sizeof(gun->ir.cal));

        // calculate aspect ratio
        double width = distance(&gun->ir.cal[SIDE_LEFT], &gun->ir.cal[SIDE_RIGHT]);
        double height = distance(&gun->ir.cal[SIDE_TOP], &gun->ir.cal[SIDE_BOTTOM]);
        gun->ar = width / height;

        gun->calibrated = time(NULL);
    }

    // save current state
    memcpy(&gun->ir.prev, &gun->ir.now, sizeof(gun->ir.prev));

    // need calibration to track
    if (!gun->calibrated)
        return;

    // do magick if less than 4 visible
    if (npoints < 4) {
        // find out what sides are visible
        int nside = 0, pside[4];
        for (int i = 0; i < 4; i++) {
            if (xwii_event_ir_is_valid(&gun->ir.now[i]))
                pside[nside++] = i;
        }

        // adjust calibrated quad for current scale based on known points
        double scale = distance(&gun->ir.now[pside[0]], &gun->ir.now[pside[1]]) / distance(&gun->ir.cal[pside[0]], &gun->ir.cal[pside[1]]);
        for (int i = 0; i < 4; i++) {
            gun->ir.adj[i].x = gun->ir.cal[i].x * scale;
            gun->ir.adj[i].y = gun->ir.cal[i].y * scale;
        }

        // rotate all points based on angle difference of known points
        double angle = line_angle(&gun->ir.adj[pside[0]], &gun->ir.adj[pside[1]]) - line_angle(&gun->ir.now[pside[0]], &gun->ir.now[pside[1]]);
        struct xwii_event_abs *anchor = &gun->ir.adj[pside[0]];
        for (int i = 0; i < 4; i++) {
            if (i == pside[0])
                continue;

            rotate(anchor, angle, &gun->ir.adj[i]);
        }

        // transform quad to anchor
        int diffx = anchor->x - gun->ir.now[pside[0]].x;
        int diffy = anchor->y - gun->ir.now[pside[0]].y;
        for (int i = 0; i < 4; i++) {
            gun->ir.adj[i].x -= diffx;
            gun->ir.adj[i].y -= diffy;
        }

        // snap all visible quad corners to their current position
        for (int i = 0; i < 4; i++) {
            if (xwii_event_ir_is_valid(&gun->ir.now[i]))
                memcpy(&gun->ir.adj[i], &gun->ir.now[i], sizeof(gun->ir.adj[0]));
        }
    } else {
        memcpy(&gun->ir.adj, &gun->ir.cal, sizeof(gun->ir.adj));
    }

    // calculate cursor relative position
    double width = distance(&gun->ir.adj[SIDE_LEFT], &gun->ir.adj[SIDE_RIGHT]);
    double height = distance(&gun->ir.adj[SIDE_TOP], &gun->ir.adj[SIDE_BOTTOM]);

    double vdist = (height / 2) + distance_to_line(&gun->ir.adj[SIDE_LEFT], &gun->ir.adj[SIDE_RIGHT], &gun->center);
    double hdist = (width / 2) - distance_to_line(&gun->ir.adj[SIDE_TOP], &gun->ir.adj[SIDE_BOTTOM], &gun->center);

    gun->hpos = hdist / width;
    gun->vpos = vdist / height;
    gun->offscreen = (gun->hpos < 0 || gun->vpos < 0 || gun->hpos > 1 || gun->vpos > 1);
}

/*
int xwiigun_poll(struct xwiigun *gun)
{
    int ret;

    if ((ret = poll(gun->xwii.fds, gun->xwii.iface ? 2 : 1, gun->blocking ? -1 : 0)) < 0) {
        if (ret == -EAGAIN)
            return 0;

        perror("xwiigun: poll() failed");
        return 1;
    }

    if (ret == 0)
        return 0;

    if (gun->xwii.fds[0].revents == POLLIN) {
        char *path = xwii_monitor_poll(gun->xwii.monitor);
        if (path) {
            xwiigun_try_open_device(gun, path);
            free(path);
        }
    } else if (gun->xwii.fds[0].revents != 0) {
        fprintf(stderr, "xwiigun: Monitor poll failed, bailing\n");
        return 1;
    }

    if (!gun->xwii.iface)
        return 0;

    if (gun->xwii.fds[1].revents != POLLIN && gun->xwii.fds[1].revents != 0) {
        fprintf(stderr, "xwiigun: Device poll failed, dropping device\n");
        xwiigun_close_device(gun);
        return 0;
    }

    while (true)
    {
        struct xwii_event e;

        if ((ret = xwii_iface_dispatch(gun->xwii.iface, &e, sizeof(e)))) {
            if (ret == -EAGAIN)
                return 0;

            fprintf(stderr, "xwiigun: Device dispatch failed, dropping device\n");
            xwiigun_close_device(gun);
            return 0;
        }

        switch (e.type) {
            case XWII_EVENT_KEY:
            {
                gun->keys[e.v.key.code] = e.v.key.state;

                if (gun->trigger_rumble && e.v.key.code == XWII_KEY_B)
                    xwii_iface_rumble(gun->xwii.iface, e.v.key.state);

                break;
            }

            case XWII_EVENT_IR:
            {
                handle_ir(gun, &e);
                break;
            }

            case XWII_EVENT_ACCEL:
            {
                if (gun->accel.x == 0 && gun->accel.y == 0 && gun->accel.z == 0) {
                    memcpy(&gun->accel, &e.v.abs[0], sizeof(gun->accel));
                } else {
                    gun->accel.x += (e.v.abs[0].x - gun->accel.x) * ACCEL_SMOOTH;
                    gun->accel.y += (e.v.abs[0].y - gun->accel.y) * ACCEL_SMOOTH;
                    gun->accel.z += (e.v.abs[0].z - gun->accel.z) * ACCEL_SMOOTH;
                }
                break;
            }

            case XWII_EVENT_WATCH:
            {
                if (xwii_iface_available(gun->xwii.iface) == 0) {
                    fprintf(stderr, "xwiigun: Device disconnected (no interfaces available)\n");
                    xwiigun_close_device(gun);
                    return 0;
                } else {
                    xwiigun_try_open_ifaces(gun);
                }
                break;
            }

            default:
            {
                fprintf(stderr, "xwiigun: Unhandled event type: %d\n", e.type);
                break;
            }
        }
    }

    return 0;
}
*/
