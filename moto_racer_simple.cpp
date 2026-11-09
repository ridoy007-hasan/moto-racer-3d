// =====================================================================
//  MOTO RACER 3D  (fixed version: Highway-Rider style camera)
//
//  Compile (Linux): g++ moto_racer_simple.cpp -o moto -lGL -lGLU -lglut -lm
//  Windows        : g++ moto_racer_simple.cpp -o moto.exe -lfreeglut -lopengl32 -lglu32
//
//  Keys (HOLD them):  W = speed up   S = brake   A = left   D = right
//         SPACE = nitro   Z = kick left     X = kick right
//         R = restart     ESC = quit
// =====================================================================
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI    3.14159265f
#define TRACK 2200.0f      // race length
#define NCARS 50           // traffic cars

const int W = 1000, H = 650;

// ---------------------------------------------------------------- camera tuning
const float CAM_DIST   = 9.0f;    // how far behind the bike (bigger = bike smaller)
const float CAM_HEIGHT = 4.4f;    // camera height
const float CAM_PITCH  = 6.0f;    // look-down angle (smaller = bike lower on screen)

// ---------------------------------------------------------------- data
struct Bike {
    float s, u;                    // s = distance on road, u = left/right position
    float speed, target;           // current speed, wanted speed
    float health, nitro, boost;
    float wantU, prevU, lean;
    float kickTimer, cooldown, stun, hit, invul;
    int   kickSide, done, place;
    float r, g, b;
};
struct Car { float s, u, speed, len, wid, hgt, r, g, b; };

Bike bike[4];
Car  car[NCARS];
int   state = 0;                   // 0 = countdown, 1 = racing, 2 = player finished
int   finishedCount = 0;
float clockT = 0, raceTime = 0;
int   frameNo = 0;
bool  keys[256];                   // which keys are held down right now
float frameDt = 0.016f;            // real frame time
float camU = 0;                    // smooth camera side position
int   lastSteer = 0;
float endT = 0;                    // time since player finished

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------- road shape
// x position of the road centre at distance s  (road bends left and right)
float roadX(float s) {
    float t = s / 300.0f;                  // straight at the start
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (13.0f * sinf(s * 0.012f) + 5.0f * sinf(s * 0.03f));
}

// road direction (degrees) at distance s
float roadAngle(float s) {
    return atanf((roadX(s + 3) - roadX(s - 3)) / 6.0f) * 180.0f / PI;
}

// ---------------------------------------------------------------- game setup
void resetGame() {
    state = 0; clockT = 0; raceTime = 0; finishedCount = 0;
    lastSteer = 0;
    endT = 0;

    float startU[4] = { 1.5f, -1.5f, -4.5f, 4.5f };   // four lanes side by side
    float startS[4] = { 1, 1, 1, 1 };                  // all on the same start line
    float col[4][3] = { {0.9f,0.1f,0.1f}, {0.1f,0.4f,0.95f}, {0.95f,0.75f,0.1f}, {0.2f,0.8f,0.3f} };
    for (int i = 0; i < 4; i++) {
        bike[i] = Bike();                              // all zero
        bike[i].s = startS[i];  bike[i].u = startU[i];  bike[i].wantU = startU[i];
        bike[i].health = 100;   bike[i].nitro = 100;
        bike[i].r = col[i][0];  bike[i].g = col[i][1];  bike[i].b = col[i][2];
    }
    camU = bike[0].u * 0.7f;
    srand(5);
    float s = 80;
    for (int k = 0; k < NCARS; k++) {
        s += 35 + rand() % 25;
        car[k].s = s;
        car[k].u = ((rand() % 3) - 1) * 4.0f;           // lane -4, 0 or 4
        bool truck = (rand() % 6 == 0);
        car[k].len = truck ? 8.0f : 4.4f;
        car[k].wid = truck ? 2.5f : 2.0f;
        car[k].hgt = truck ? 3.0f : 1.5f;
        car[k].speed = truck ? 8 + rand() % 3 : 10 + rand() % 6;
        car[k].r = 0.2f + (rand() % 80) / 100.0f;
        car[k].g = 0.2f + (rand() % 80) / 100.0f;
        car[k].b = 0.2f + (rand() % 80) / 100.0f;
    }
    srand(1234);
}

// ---------------------------------------------------------------- fighting
void hurt(Bike &b, float dmg) {
    if (b.invul > 0 || b.stun > 0) return;
    b.health -= dmg;
    b.invul = 0.5f;
    if (b.health <= 0) { b.stun = 2.5f; b.health = 60; b.speed = 0; }   // crashed, then respawn
}

void kick(int i, int side) {            // side = -1 left, +1 right
    Bike &b = bike[i];
    if (b.cooldown > 0 || b.stun > 0) return;
    b.cooldown = 0.6f;  b.kickTimer = 0.3f;  b.kickSide = side;
    for (int j = 0; j < 4; j++) {
        if (j == i) continue;
        Bike &o = bike[j];
        float ds = fabsf(o.s - b.s);
        float du = (o.u - b.u) * side;
        if (ds < 3.2f && du > 0.3f && du < 3.3f) {
            o.wantU = clampf(o.wantU + side * 1.8f, -5, 5);
            o.speed = fmaxf(0, o.speed - 5);
            o.hit = 0.3f;
            hurt(o, 12);
        }
    }
}

// ---------------------------------------------------------------- AI rider
void ai(int i, float dt) {
    Bike &b = bike[i];
    b.target = 22 + i + clampf((bike[0].s - b.s) * 0.03f, -2, 4);   // slow rider catches up a bit

    bool danger = false;                                   // look for traffic ahead
    for (int k = 0; k < NCARS; k++) {
        float ahead = car[k].s - b.s;
        if (ahead > 0 && ahead < 20 && fabsf(car[k].u - b.u) < 2.6f) {
            danger = true;
            float dir = (b.u < car[k].u) ? -1.0f : 1.0f;
            if (fabsf(b.u + dir * 3.5f) > 5) dir = -dir;
            b.wantU = clampf(b.u + dir * 3.5f, -5, 5);
            if (b.target > car[k].speed + 2) b.target = car[k].speed + 2;
        }
    }
    if (!danger && rand() % 150 == 0) {                    // sometimes go and fight a rival
        int j = rand() % 4;
        if (j != i && fabsf(bike[j].s - b.s) < 12)
            b.wantU = clampf(bike[j].u + (b.u > bike[j].u ? 1.9f : -1.9f), -5, 5);
    }
    for (int j = 0; j < 4; j++) {                          // kick rivals who are beside us
        if (j == i) continue;
        float ds = fabsf(bike[j].s - b.s), du = bike[j].u - b.u;
        if (ds < 3.2f && fabsf(du) > 0.3f && fabsf(du) < 3.3f && rand() % 100 < 2)
            kick(i, du > 0 ? 1 : -1);
    }
    if (b.nitro > 80 && rand() % 400 == 0) { b.nitro -= 30; b.boost = 1.5f; }
}

// ---------------------------------------------------------------- update
void moveBike(int i, float dt) {
    Bike &b = bike[i];
    if (b.kickTimer > 0) b.kickTimer -= dt;
    if (b.cooldown > 0)  b.cooldown -= dt;
    if (b.hit > 0)       b.hit -= dt;
    if (b.invul > 0)     b.invul -= dt;
    b.prevU = b.u;

    if (b.stun > 0) { b.stun -= dt; b.speed = 0; b.lean = 0; return; }
    if (i > 0 && !b.done) ai(i, dt);
    if (b.done) b.target = 8;

    // sideways movement (player is faster and snappy)
    float lim = (i == 0) ? 8.5f : 5.0f;
    b.wantU = clampf(b.wantU, -lim, lim);
    float step = ((i == 0) ? 16 : 9) * dt;
    if (b.u < b.wantU) b.u = fminf(b.u + step, b.wantU);
    else               b.u = fmaxf(b.u - step, b.wantU);

    // forward movement
    float t = b.target;
    if (b.boost > 0) { t += 12; b.boost -= dt; }
    if (i == 0 && fabsf(b.u) > 6) t = fminf(t, 12);        // off road = slow
    if (b.speed < t) b.speed += 14 * dt; else b.speed -= 18 * dt;
    if (b.speed < 0) b.speed = 0;
    b.s += b.speed * dt;                                   // player follows the road like everyone else
    b.u = clampf(b.u, -lim, lim);
    b.nitro = fminf(100, b.nitro + 4 * dt);

    float vel = (b.u - b.prevU) / fmaxf(dt, 0.001f);       // leaning
    b.lean += (clampf(vel * 1.5f, -22, 22) - b.lean) * (1.0f - expf(-12.0f * dt));
}

void update(float dt) {
    clockT += dt;
    if (state == 2) { endT += dt; if (endT > 8.0f) { resetGame(); return; } }
    if (state == 0) {
        if (clockT > 3.5f) { state = 1; bike[0].target = 24; }
        return;
    }
    if (state == 1) raceTime += dt;

    Bike &pl = bike[0];                                    // player controls (keys are held down)
    if (pl.stun <= 0 && !pl.done) {
        int steer = (keys['d'] ? 1 : 0) - (keys['a'] ? 1 : 0);
        if (steer)          pl.wantU = clampf(pl.u + steer * 4.0f, -8.5f, 8.5f);   // go immediately
        else if (lastSteer) pl.wantU = pl.u;                                        // stop when key released
        lastSteer = steer;
        if (keys['w']) pl.target = fminf(pl.target + 16 * dt, 32);
        if (keys['s']) pl.target = fmaxf(pl.target - 16 * dt, 0);
    }

    for (int i = 0; i < 4; i++) moveBike(i, dt);
    for (int k = 0; k < NCARS; k++) car[k].s += car[k].speed * dt;

    // bike <-> traffic
    for (int i = 0; i < 4; i++) {
        Bike &b = bike[i];
        for (int k = 0; k < NCARS; k++) {
            float rel = car[k].s - b.s, half = car[k].len / 2 + 1;
            if (fabsf(rel) < half && fabsf(car[k].u - b.u) < car[k].wid / 2 + 0.6f && b.stun <= 0) {
                if (rel > 0) {                            // we hit the back of a car
                    b.s = car[k].s - half;
                    b.speed = fminf(b.speed, car[k].speed * 0.5f);
                    hurt(b, 15);
                } else {                                  // we are beside the car
                    b.u += (b.u > car[k].u ? 1 : -1) * 6 * dt;
                }
            }
        }
    }
    // bike <-> bike
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) {
        Bike &a = bike[i], &b = bike[j];
        if (fabsf(a.s - b.s) < 2.2f && fabsf(a.u - b.u) < 1.0f) {
            float push = (a.u >= b.u) ? 0.08f : -0.08f;
            a.u += push;  b.u -= push;
            a.wantU = clampf(a.wantU + push * 3, -5, 5);
            b.wantU = clampf(b.wantU - push * 3, -5, 5);
        }
    }
    // finish line
    for (int i = 0; i < 4; i++) {
        if (!bike[i].done && bike[i].s >= TRACK) {
            bike[i].done = 1;
            bike[i].place = ++finishedCount;
            if (i == 0) {
                state = 2;
                printf("You finished number %d in %.1f seconds\n", bike[i].place, raceTime);
            }
        }
    }
}

// ---------------------------------------------------------------- ranking
bool isAhead(int j, int i) {                 // is bike j ahead of bike i ?
    if (bike[j].done && bike[i].done) return bike[j].place < bike[i].place;
    if (bike[j].done) return true;
    if (bike[i].done) return false;
    return bike[j].s > bike[i].s;
}
int rankOf(int i) {
    int r = 1;
    for (int j = 0; j < 4; j++) if (j != i && isAhead(j, i)) r++;
    return r;
}

// ================================================================= DRAWING
// A box that stands on y = 0, centred in x and z.
void box(float w, float h, float d, float r, float g, float b) {
    float x = w / 2, z = d / 2;
    glBegin(GL_QUADS);
    glColor3f(r, g, b);                                              // top
    glVertex3f(-x, h, z);  glVertex3f(x, h, z);  glVertex3f(x, h, -z);  glVertex3f(-x, h, -z);
    glColor3f(r * 0.75f, g * 0.75f, b * 0.75f);                      // back (+z)
    glVertex3f(-x, 0, z);  glVertex3f(x, 0, z);  glVertex3f(x, h, z);  glVertex3f(-x, h, z);
    glColor3f(r * 0.6f, g * 0.6f, b * 0.6f);                         // front (-z)
    glVertex3f(-x, 0, -z); glVertex3f(x, 0, -z); glVertex3f(x, h, -z); glVertex3f(-x, h, -z);
    glColor3f(r * 0.85f, g * 0.85f, b * 0.85f);                      // right side (+x)
    glVertex3f(x, 0, z);   glVertex3f(x, 0, -z); glVertex3f(x, h, -z); glVertex3f(x, h, z);
    glColor3f(r * 0.65f, g * 0.65f, b * 0.65f);                      // left side (-x)
    glVertex3f(-x, 0, z);  glVertex3f(-x, 0, -z); glVertex3f(-x, h, -z); glVertex3f(-x, h, z);
    glEnd();
}
// A box placed at (x,y,z)
void part(float x, float y, float z, float w, float h, float d, float r, float g, float b) {
    glPushMatrix();
    glTranslatef(x, y, z);
    box(w, h, d, r, g, b);
    glPopMatrix();
}
// A round shape (polygon) facing the camera
void disc(float x, float y, float z, float rad, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_POLYGON);
    for (int a = 0; a < 360; a += 20)
        glVertex3f(x + rad * cosf(a * PI / 180), y + rad * sinf(a * PI / 180), z);
    glEnd();
}
// A flat strip of road: lateral range a..b from road centre, from s to s+len
void strip(float a, float b, float s, float len, float y) {
    float x1 = roadX(s), x2 = roadX(s + len);
    glBegin(GL_QUADS);
    glVertex3f(x1 + a, y, -s);        glVertex3f(x1 + b, y, -s);
    glVertex3f(x2 + b, y, -s - len);  glVertex3f(x2 + a, y, -s - len);
    glEnd();
}

// ---------------------------------------------------------------- road, buildings, trees
void drawRoad(float s0, float s1) {
    s0 = floorf(s0 / 4) * 4;
    glColor3f(0.25f, 0.62f, 0.25f);                                         // grass
    for (float s = s0; s < s1; s += 4) strip(-80, 80, s, 4, -0.02f);
    glColor3f(0.68f, 0.68f, 0.64f);                                         // sidewalks
    for (float s = s0; s < s1; s += 4) { strip(-9, -6, s, 4, 0); strip(6, 9, s, 4, 0); }
    glColor3f(0.2f, 0.2f, 0.22f);                                           // asphalt
    for (float s = s0; s < s1; s += 4) strip(-6, 6, s, 4, 0);
    glColor3f(1, 1, 1);                                                     // white lines
    for (float s = s0; s < s1; s += 4) {
        strip(-5.7f, -5.5f, s, 4, 0.01f);  strip(5.5f, 5.7f, s, 4, 0.01f);
        if (((int)(s / 4)) % 2 == 0) { strip(-2.08f, -1.92f, s, 4, 0.01f); strip(1.92f, 2.08f, s, 4, 0.01f); }
    }
}

void drawLine(float s) {                              // checkered start / finish line (flat)
    for (int i = 0; i < 12; i++) for (int j = 0; j < 2; j++) {
        if ((i + j) % 2) glColor3f(0, 0, 0); else glColor3f(1, 1, 1);
        strip(-6 + i, -5 + i, s + j, 1, 0.02f);
    }
}
void drawArch(float s) {                              // finish arch
    part(roadX(s) - 6.8f, 0, -s, 0.5f, 7, 0.5f, 0.8f, 0.8f, 0.8f);
    part(roadX(s) + 6.8f, 0, -s, 0.5f, 7, 0.5f, 0.8f, 0.8f, 0.8f);
    for (int i = 0; i < 14; i++)
        part(roadX(s) - 6.5f + i, 7, -s, 1, 1, 0.4f, (i % 2) ? 0 : 1, (i % 2) ? 0 : 1, (i % 2) ? 0 : 1);
}

unsigned hashi(int i, int side) {
    unsigned x = i * 374761393u + (side + 2) * 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    return x ^ (x >> 16);
}

void drawBuilding(int i, int side) {
    float s = i * 14.0f;
    unsigned h = hashi(i, side);
    float w = 7 + h % 6, height = 5 + (h >> 5) % 9, depth = 9;
    float wall[6][3]  = { {0.8f,0.55f,0.45f}, {0.85f,0.8f,0.65f}, {0.55f,0.62f,0.75f},
                          {0.7f,0.7f,0.72f},  {0.75f,0.45f,0.4f}, {0.55f,0.7f,0.55f} };
    float shop[5][3]  = { {0.9f,0.15f,0.15f}, {0.15f,0.6f,0.9f}, {0.95f,0.7f,0.1f},
                          {0.2f,0.75f,0.3f},  {0.85f,0.3f,0.7f} };
    int c = (h >> 9) % 6, a = (h >> 13) % 5;

    glPushMatrix();
    glTranslatef(roadX(s) + side * (10.5f + w / 2), 0, -s);
    box(w, height, depth, wall[c][0], wall[c][1], wall[c][2]);

    float fx = -side * (w / 2 + 0.03f);                  // the wall that faces the road
    glBegin(GL_QUADS);
    glColor3f(0.3f, 0.5f, 0.65f);                        // shop glass
    glVertex3f(fx, 0.2f, -3.8f);  glVertex3f(fx, 0.2f, 1.2f);  glVertex3f(fx, 2.4f, 1.2f);  glVertex3f(fx, 2.4f, -3.8f);
    glColor3f(0.35f, 0.2f, 0.1f);                        // door
    glVertex3f(fx, 0, 2.0f);  glVertex3f(fx, 0, 3.6f);  glVertex3f(fx, 2.4f, 3.6f);  glVertex3f(fx, 2.4f, 2.0f);
    glColor3f(0.65f, 0.82f, 0.95f);                      // upper windows
    for (float y = 3.4f; y + 1.3f < height; y += 2.3f)
        for (float z0 = -4.0f; z0 + 1.4f < 4.5f; z0 += 2.5f) {
            glVertex3f(fx, y, z0);  glVertex3f(fx, y, z0 + 1.4f);
            glVertex3f(fx, y + 1.3f, z0 + 1.4f);  glVertex3f(fx, y + 1.3f, z0);
        }
    glEnd();
    part(-side * (w / 2 + 0.9f), 2.6f, 0, 1.8f, 0.25f, depth * 0.95f, shop[a][0], shop[a][1], shop[a][2]);   // awning
    part(-side * (w / 2 - 0.4f), height, 0, 0.3f, 1.4f, depth * 0.7f,
         shop[(a + 2) % 5][0], shop[(a + 2) % 5][1], shop[(a + 2) % 5][2]);                                   // roof sign
    glPopMatrix();
}

void drawTreeOrLamp(int i, int side) {
    float s = i * 14.0f + 7;
    float x = roadX(s) + side * 7.6f;
    if (hashi(i, side + 7) % 2) {                       // tree
        part(x, 0, -s, 0.4f, 2.2f, 0.4f, 0.4f, 0.25f, 0.1f);
        part(x, 2.0f, -s, 2.4f, 2.0f, 2.4f, 0.1f, 0.5f, 0.15f);
        part(x, 3.8f, -s, 1.6f, 1.4f, 1.6f, 0.15f, 0.6f, 0.2f);
    } else {                                            // street lamp
        part(x, 0, -s, 0.2f, 6, 0.2f, 0.25f, 0.25f, 0.28f);
        part(x - side * 0.8f, 6, -s, 1.6f, 0.15f, 0.2f, 0.25f, 0.25f, 0.28f);
        part(x - side * 1.5f, 5.8f, -s, 0.6f, 0.2f, 0.4f, 1, 0.95f, 0.5f);
    }
}

// ---------------------------------------------------------------- vehicles
void drawCar(Car &c) {
    glPushMatrix();
    glTranslatef(roadX(c.s) + c.u, 0, -c.s);
    glRotatef(-roadAngle(c.s), 0, 1, 0);                                         // follow the road bend
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)      // wheels
        part(sx * c.wid * 0.5f, 0, sz * c.len * 0.3f, 0.3f, 0.8f, 0.9f, 0.05f, 0.05f, 0.05f);
    if (c.len < 6) {                                                             // car
        part(0, 0.3f, 0, c.wid * 0.96f, 0.7f, c.len, c.r, c.g, c.b);
        part(0, 1.0f, 0.3f, c.wid * 0.85f, 0.65f, c.len * 0.5f, 0.2f, 0.25f, 0.35f);
    } else {                                                                     // truck
        part(0, 0.4f, 1.25f, c.wid, c.hgt - 0.4f, c.len - 2.5f, 0.85f, 0.85f, 0.85f);
        part(0, 0.4f, -2.75f, c.wid, c.hgt * 0.65f, 2.5f, c.r, c.g, c.b);
    }
    glBegin(GL_QUADS);                                                           // red tail lights
    glColor3f(1, 0.1f, 0.1f);
    float z = c.len / 2 + 0.02f;
    for (int k = -1; k <= 1; k += 2) {
        glVertex3f(k * c.wid * 0.35f - 0.2f, 0.5f, z);  glVertex3f(k * c.wid * 0.35f + 0.2f, 0.5f, z);
        glVertex3f(k * c.wid * 0.35f + 0.2f, 0.8f, z);  glVertex3f(k * c.wid * 0.35f - 0.2f, 0.8f, z);
    }
    glEnd();
    glPopMatrix();
}

void drawBike(int id) {
    Bike &b = bike[id];
    float jr = b.b * 0.5f + 0.1f, jg = b.r * 0.4f + 0.1f, jb = b.g * 0.5f + 0.1f;   // jacket colour
    glPushMatrix();
    glTranslatef(roadX(b.s) + b.u, 0, -b.s);
    glRotatef(-roadAngle(b.s), 0, 1, 0);                 // follow the road bend
    glScalef(1.4f, 1.4f, 1.4f);                          // size of the bike

    glBegin(GL_QUADS);                                   // shadow
    glColor3f(0.1f, 0.15f, 0.1f);
    glVertex3f(-0.6f, 0.03f, -1.6f);  glVertex3f(0.6f, 0.03f, -1.6f);
    glVertex3f(0.6f, 0.03f, 1.6f);    glVertex3f(-0.6f, 0.03f, 1.6f);
    glEnd();

    if (b.stun > 0) glRotatef(80, 0, 0, 1);              // crashed: lying down
    else            glRotatef(-b.lean, 0, 0, 1);         // lean while turning

    // ---- front part
    part(0, 0, -1.0f, 0.26f, 0.9f, 0.9f, 0.05f, 0.05f, 0.05f);            // front wheel
    part(0, 0.9f, -1.0f, 0.08f, 0.02f, 0.6f, 0.8f, 0.8f, 0.85f);          // front rim
    part(0, 0.5f, -0.85f, 0.5f, 0.45f, 0.5f, b.r, b.g, b.b);              // front fairing
    part(0, 0.95f, -0.65f, 0.4f, 0.4f, 0.05f, 0.6f, 0.8f, 0.9f);          // wind screen
    part(0, 1.05f, -0.4f, 1.0f, 0.1f, 0.1f, 0.75f, 0.75f, 0.8f);          // handle bar
    part(-0.55f, 1.1f, -0.45f, 0.12f, 0.18f, 0.1f, b.r, b.g, b.b);        // mirrors
    part( 0.55f, 1.1f, -0.45f, 0.12f, 0.18f, 0.1f, b.r, b.g, b.b);
    // ---- middle part
    part(0, 0.55f, -0.2f, 0.55f, 0.55f, 0.8f, b.r, b.g, b.b);             // fuel tank
    part(0, 0.5f, 0.55f, 0.5f, 0.45f, 0.9f, b.r * 0.85f, b.g * 0.85f, b.b * 0.85f);   // tail
    part(0, 0.95f, 0.55f, 0.12f, 0.02f, 0.9f, 1, 1, 1);                   // white stripe
    part(0, 0.97f, 0.45f, 0.4f, 0.12f, 0.6f, 0.08f, 0.08f, 0.08f);        // seat
    part(0, 0.5f, 1.0f, 0.5f, 0.3f, 0.4f, b.r * 0.85f, b.g * 0.85f, b.b * 0.85f);   // rear mud guard
    // ---- rider legs (kick animation)
    for (int side = -1; side <= 1; side += 2) {
        float ang = 0;
        if (b.kickTimer > 0 && b.kickSide == side) ang = side * 80 * sinf(b.kickTimer / 0.3f * PI);
        glPushMatrix();
        glTranslatef(side * 0.34f, 1.0f, 0.3f);
        glRotatef(ang, 0, 0, 1);
        glTranslatef(0, -0.7f, 0);
        box(0.2f, 0.7f, 0.28f, 0.12f, 0.12f, 0.35f);                       // leg
        part(0, -0.05f, -0.1f, 0.24f, 0.15f, 0.45f, 0.05f, 0.05f, 0.05f);  // boot
        glPopMatrix();
    }
    // ---- rider body
    part(0, 1.0f, 0.35f, 0.6f, 0.85f, 0.38f, jr, jg, jb);                  // jacket
    part(0, 1.05f, 0.55f, 0.14f, 0.7f, 0.02f, 1, 1, 1);                    // back stripe
    part(-0.4f, 1.4f, -0.05f, 0.14f, 0.14f, 0.95f, jr, jg, jb);            // arms
    part( 0.4f, 1.4f, -0.05f, 0.14f, 0.14f, 0.95f, jr, jg, jb);
    part(0, 1.78f, 0.3f, 0.5f, 0.5f, 0.55f, b.r, b.g, b.b);                // helmet
    part(0, 1.95f, 0.0f, 0.4f, 0.18f, 0.1f, 0.05f, 0.05f, 0.1f);           // dark visor
    part(0, 2.28f, 0.3f, 0.1f, 0.02f, 0.55f, 1, 1, 1);                     // helmet stripe
    // ---- rear part (nearest to camera, so drawn last)
    part(0, 0, 1.05f, 0.3f, 0.9f, 0.9f, 0.05f, 0.05f, 0.05f);             // rear wheel
    part(0, 0.9f, 1.05f, 0.08f, 0.02f, 0.6f, 0.8f, 0.8f, 0.85f);          // rear rim
    part(-0.38f, 0.2f, 0.9f, 0.14f, 0.22f, 1.1f, 0.55f, 0.55f, 0.6f);     // exhaust pipes
    part( 0.38f, 0.2f, 0.9f, 0.14f, 0.22f, 1.1f, 0.55f, 0.55f, 0.6f);
    part(0, 0.95f, 1.35f, 0.3f, 0.12f, 0.14f, 1, 0.1f, 0.1f);              // red tail light

    if (b.boost > 0) {                                                     // nitro flame
        disc(0.38f, 0.3f, 1.7f, 0.3f, 1, 0.5f, 0.1f);
        disc(-0.38f, 0.3f, 1.7f, 0.3f, 1, 0.5f, 0.1f);
        disc(0.38f, 0.3f, 1.8f, 0.15f, 1, 1, 0.3f);
        disc(-0.38f, 0.3f, 1.8f, 0.15f, 1, 1, 0.3f);
    }
    if (b.hit > 0) {                                                       // hit star
        glColor3f(1, 0.9f, 0.1f);
        glBegin(GL_POLYGON);
        for (int a = 0; a < 360; a += 45) {
            float r = (a % 90 == 0) ? 0.9f : 0.4f;
            glVertex3f(r * cosf(a * PI / 180), 2.4f + r * sinf(a * PI / 180), 1.2f);
        }
        glEnd();
    }
    glPopMatrix();
}

// ---------------------------------------------------------------- whole world
void drawWorld() {
    float ps = bike[0].s;
    glDisable(GL_DEPTH_TEST);                          // flat ground first, everything else is drawn on top
    drawRoad(ps - 20, ps + 260);
    drawLine(0);
    drawLine(TRACK);
    glEnable(GL_DEPTH_TEST);                           // now real 3D objects

    drawArch(TRACK);
    int i0 = (int)floorf((ps - 25) / 14), i1 = (int)((ps + 200) / 14);
    for (int i = i0; i <= i1; i++) {
        if (i < -2) continue;
        for (int side = -1; side <= 1; side += 2) {
            drawBuilding(i, side);
            drawTreeOrLamp(i, side);
        }
    }
    for (int k = 0; k < NCARS; k++)
        if (car[k].s > ps - 20 && car[k].s < ps + 200) drawCar(car[k]);
    for (int i = 0; i < 4; i++) drawBike(i);
}

// ---------------------------------------------------------------- HUD (2D)
void rectQ(float x, float y, float w, float h, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex3f(x, y, 0);  glVertex3f(x + w, y, 0);  glVertex3f(x + w, y + h, 0);  glVertex3f(x, y + h, 0);
    glEnd();
}

void drawHUD() {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);  glLoadIdentity();  glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);   glLoadIdentity();
    Bike &p = bike[0];

    // speed / nitro / health bars (bottom right)
    rectQ(W - 260, 15, 245, 100, 0.1f, 0.1f, 0.1f);
    rectQ(W - 245, 90, 215, 14, 0.3f, 0.3f, 0.3f);  rectQ(W - 245, 90, 215 * clampf(p.speed / 45, 0, 1), 14, 0.2f, 0.9f, 0.2f);
    rectQ(W - 245, 60, 215, 14, 0.3f, 0.3f, 0.3f);  rectQ(W - 245, 60, 215 * p.nitro / 100, 14, 0.2f, 0.6f, 1);
    rectQ(W - 245, 30, 215, 14, 0.3f, 0.3f, 0.3f);  rectQ(W - 245, 30, 215 * p.health / 100, 14, 1 - p.health / 100, p.health / 100, 0.1f);

    // ranking board (top left): coloured square + health bar
    rectQ(15, H - 150, 170, 135, 0.1f, 0.1f, 0.1f);
    for (int r = 1; r <= 4; r++)
        for (int i = 0; i < 4; i++)
            if (rankOf(i) == r) {
                float y = H - 20 - r * 30;
                rectQ(25, y, 20, 20, bike[i].r, bike[i].g, bike[i].b);
                rectQ(55, y + 4, 120, 12, 0.3f, 0.3f, 0.3f);
                rectQ(55, y + 4, 120 * bike[i].health / 100, 12, 0.9f, 0.9f, 0.9f);
            }

    // race progress bar (top centre)
    rectQ(W / 2 - 260, H - 40, 520, 25, 0.1f, 0.1f, 0.1f);
    rectQ(W / 2 - 250, H - 30, 500, 5, 0.8f, 0.8f, 0.8f);
    for (int i = 3; i >= 0; i--) {
        float t = clampf(bike[i].s / TRACK, 0, 1);
        rectQ(W / 2 - 250 + t * 488, H - 36, 12, 17, bike[i].r, bike[i].g, bike[i].b);
    }

    if (state == 0) {                                      // start lights
        rectQ(W / 2 - 100, H / 2 - 40, 200, 80, 0.05f, 0.05f, 0.05f);
        int lit = (int)clockT + 1;  if (lit > 3) lit = 3;
        for (int k = 0; k < 3; k++) {
            if (clockT >= 3.0f)  disc(W / 2 - 60 + k * 60, H / 2, 0, 24, 0.1f, 1, 0.1f);       // GO = green
            else if (k < lit)    disc(W / 2 - 60 + k * 60, H / 2, 0, 24, 1, 0.1f, 0.1f);
            else                 disc(W / 2 - 60 + k * 60, H / 2, 0, 24, 0.3f, 0.1f, 0.1f);
        }
    }
    if (state == 2) {                                      // result medal
        int pl = p.place;
        rectQ(W / 2 - 150, H / 2 - 100, 300, 200, 0.1f, 0.1f, 0.1f);
        if (pl == 1) disc(W / 2, H / 2 + 20, 0, 55, 1, 0.85f, 0.1f);
        else if (pl == 2) disc(W / 2, H / 2 + 20, 0, 55, 0.8f, 0.8f, 0.85f);
        else if (pl == 3) disc(W / 2, H / 2 + 20, 0, 55, 0.8f, 0.5f, 0.2f);
        else disc(W / 2, H / 2 + 20, 0, 55, 0.4f, 0.4f, 0.4f);
        for (int k = 0; k < pl; k++) rectQ(W / 2 - pl * 12 + k * 24, H / 2 - 80, 16, 16, 1, 1, 1);   // place = number of squares
        glColor3f(1, 1, 1);
        glRasterPos2f(W / 2 - 80, H / 2 - 58);
        const char *msg = "Press R to restart";
        for (const char *c = msg; *c; c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    }
}

// ---------------------------------------------------------------- display
void drawSky() {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);  glLoadIdentity();  glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);   glLoadIdentity();
    glBegin(GL_QUADS);
    glColor3f(0.85f, 0.93f, 1.0f);  glVertex3f(0, H * 0.45f, 0);  glVertex3f(W, H * 0.45f, 0);
    glColor3f(0.25f, 0.5f, 0.95f);  glVertex3f(W, H, 0);          glVertex3f(0, H, 0);
    glEnd();
    disc(W * 0.8f, H * 0.85f, 0, 40, 1, 0.95f, 0.6f);          // sun
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky();

    // Highway-Rider style chase camera: behind, above, bike in lower part of screen
    Bike &p = bike[0];
    camU += (p.u * 0.7f - camU) * (1.0f - expf(-8.0f * frameDt));   // smooth lag so bike moves on screen
    float th   = atanf((roadX(p.s + 6) - roadX(p.s - 6)) / 12.0f);  // road direction
    float camX = roadX(p.s) + camU - CAM_DIST * sinf(th);

    glMatrixMode(GL_PROJECTION);  glLoadIdentity();
    glFrustum(-0.888, 0.888, -0.577, 0.577, 1.0, 350.0);
    glMatrixMode(GL_MODELVIEW);   glLoadIdentity();
    glRotatef(CAM_PITCH, 1, 0, 0);                                  // look slightly down
    glRotatef(th * 180.0f / PI, 0, 1, 0);                           // turn with the road
    glTranslatef(-camX, -CAM_HEIGHT, p.s - CAM_DIST * cosf(th));

    drawWorld();
    drawHUD();
    glutSwapBuffers();
}

// ---------------------------------------------------------------- keyboard
void keyboard(unsigned char key, int x, int y) {
    if (key >= 'A' && key <= 'Z') key += 32;
    keys[key] = true;                                         // W A S D are read in update()
    if (key == 27) exit(0);
    if (key == 'r') { resetGame(); return; }
    if (state != 1) return;
    Bike &p = bike[0];
    if (key == ' ' && p.nitro >= 30 && p.stun <= 0) { p.nitro -= 30; p.boost = 1.5f; }
    if (key == 'z') kick(0, -1);
    if (key == 'x') kick(0, 1);
}
void keyboardUp(unsigned char key, int x, int y) {
    if (key >= 'A' && key <= 'Z') key += 32;
    keys[key] = false;
}

// ---------------------------------------------------------------- timer (animation)
void timer(int v) {
    static int lastMs = 0;
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (lastMs == 0) ? 0.016f : (now - lastMs) / 1000.0f;
    lastMs = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.001f) dt = 0.001f;
    frameDt = dt;

    update(dt);
    frameNo++;
    if (frameNo % 10 == 0) {                                 // info in the title bar
        char t[160];
        sprintf(t, "Moto Racer 3D | Place %d/4 | Speed %d km/h | Health %d | Nitro %d | Time %.1f",
                rankOf(0), (int)(bike[0].speed * 4), (int)bike[0].health, (int)bike[0].nitro, raceTime);
        glutSetWindowTitle(t);
    }
    glutPostRedisplay();
    glutTimerFunc(8, timer, 0);
}

int main(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(W, H);
    glutInitWindowPosition(100, 50);
    glutCreateWindow("Moto Racer 3D");
    glClearColor(0.85f, 0.93f, 1.0f, 1.0f);                   // sky colour
    resetGame();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutIgnoreKeyRepeat(1);
    glutTimerFunc(8, timer, 0);
    glutMainLoop();
    return 0;
}
