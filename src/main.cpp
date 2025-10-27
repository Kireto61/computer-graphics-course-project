// OpenGL 3.3 Human Skeleton Movement Simulation
// ------------------------------------------------------------
// Minimal example using GLFW + GLAD + GLM that draws a stick-figure
// skeleton (hierarchical bones) and animates a simple walk cycle.
// Now with a solid triangulated SPHERE for the head.
//
// Build (Linux/Mac):
//   c++ -std=c++17 main.cpp -lglfw -ldl -framework Cocoa -framework IOKit -framework CoreVideo \
//      -I/path/to/glad/include -I/path/to/glm -L/path/to/glad/lib -lglad -o skel
// (adjust frameworks/libs per platform; on Linux remove the frameworks, keep -ldl -lGL)
//
// Dependencies:
//   - GLFW >= 3.3
//   - GLAD (OpenGL 3.3 core) or another loader
//   - GLM
// ------------------------------------------------------------

#include <cstdio>
#include <vector>
#include <string>
#include <cmath>
#include <optional>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ------------------------------------------------------------
// Shader sources (position + color; no lighting)
// ------------------------------------------------------------
static const char* kVS = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vColor;
void main(){
    vColor = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

static const char* kFS = R"GLSL(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main(){
    FragColor = vec4(vColor, 1.0);
}
)GLSL";

static GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

static GLuint makeProgram(){
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){
        char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log);
        std::fprintf(stderr, "Link error: %s\n", log);
    }
    glDeleteShader(vs); glDeleteShader(fs);
    return p;
}

// ------------------------------------------------------------
// Bones / Skeleton
// ------------------------------------------------------------
struct Bone {
    int parent;                 // -1 for root
    glm::vec3 bindOffset;       // from parent to this joint in bind pose
    glm::vec3 eulerDeg;         // current local rotation (XYZ degrees)
    float length;               // bone visual length (for child end)
    glm::mat4 global;           // world-space transform of this joint
};

struct Skeleton {
    std::vector<Bone> bones;

    int addBone(int parent, glm::vec3 bindOffset, float length){
        Bone b{}; b.parent = parent; b.bindOffset = bindOffset; b.eulerDeg = glm::vec3(0); b.length = length; b.global = glm::mat4(1);
        bones.push_back(b);
        return (int)bones.size()-1;
    }

    static glm::mat4 rotXYZ(const glm::vec3& deg){
        glm::vec3 r = glm::radians(deg);
        glm::mat4 Rx = glm::rotate(glm::mat4(1), r.x, {1,0,0});
        glm::mat4 Ry = glm::rotate(glm::mat4(1), r.y, {0,1,0});
        glm::mat4 Rz = glm::rotate(glm::mat4(1), r.z, {0,0,1});
        return Rz * Ry * Rx; // XYZ intrinsic
    }

    void updateGlobals(){
        for(size_t i=0;i<bones.size();++i){
            const int p = bones[i].parent;
            glm::mat4 T = glm::translate(glm::mat4(1), bones[i].bindOffset);
            glm::mat4 R = rotXYZ(bones[i].eulerDeg);
            glm::mat4 local = T * R;
            bones[i].global = (p >= 0) ? bones[p].global * local : local;
        }
    }
};

// Build a very small human-like hierarchy
Skeleton makeHuman() {
    Skeleton s;
    // Reference scale ~1.8 m tall stick figure, Y is up.
    const float pelvisH = 1.0f;     // baseline hip height
    const float spineLen = 0.4f;    // spine
    const float neckLen  = 0.1f;
    const float headLen  = 0.22f;

    const float upperLeg = 0.45f, lowerLeg = 0.45f, footLen = 0.18f;
    const float upperArm = 0.30f, lowerArm = 0.30f, handLen = 0.12f;
    const float hipWidth = 0.18f, shoulderWidth = 0.28f;

    // ------------------------------------------------------------
    // Root pelvis center — raised high so "middle line" starts above hips
    // ------------------------------------------------------------
    int root = s.addBone(-1, {0, pelvisH + 0.30f, 0}, 0.0f);   // pelvis center (much higher)
    int spine = s.addBone(root, {0, 0.0f, 0}, spineLen);
    int neck  = s.addBone(spine, {0, spineLen, 0}, neckLen);
    int head  = s.addBone(neck,  {0, neckLen, 0}, headLen);

    // ------------------------------------------------------------
    // Legs — attach far below pelvis so torso line clearly above them
    // ------------------------------------------------------------
    int hipL = s.addBone(root, {+hipWidth * 0.5f, -0.30f, 0}, upperLeg);
    int kneeL = s.addBone(hipL, {0, -upperLeg, 0}, lowerLeg);
    int ankleL = s.addBone(kneeL, {0, -lowerLeg, 0}, footLen);

    int hipR = s.addBone(root, {-hipWidth * 0.5f, -0.30f, 0}, upperLeg);
    int kneeR = s.addBone(hipR, {0, -upperLeg, 0}, lowerLeg);
    int ankleR = s.addBone(kneeR, {0, -lowerLeg, 0}, footLen);

    // ------------------------------------------------------------
    // Arms — attach at shoulder level
    // ------------------------------------------------------------
    int shoulderL = s.addBone(spine, {+shoulderWidth * 0.5f, spineLen, 0}, upperArm);
    int elbowL = s.addBone(shoulderL, {0, -upperArm, 0}, lowerArm);
    int wristL = s.addBone(elbowL, {0, -lowerArm, 0}, handLen);

    int shoulderR = s.addBone(spine, {-shoulderWidth * 0.5f, spineLen, 0}, upperArm);
    int elbowR = s.addBone(shoulderR, {0, -upperArm, 0}, lowerArm);
    int wristR = s.addBone(elbowR, {0, -lowerArm, 0}, handLen);

    (void)ankleL; (void)wristL; (void)wristR;
    return s;
}



// ------------------------------------------------------------
// Geometry helpers
// ------------------------------------------------------------
struct LineVertex{ glm::vec3 pos; glm::vec3 col; };
struct TriVertex { glm::vec3 pos; glm::vec3 col; };

static glm::vec3 jointPos(const Bone& b){ return glm::vec3(b.global[3]); }
static glm::vec3 endpointPos(const Bone& b){
    glm::vec4 p = b.global * glm::vec4(0, -b.length, 0, 1);
    return glm::vec3(p);
}

static void appendLine(std::vector<LineVertex>& v, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c){
    v.push_back({a,c}); v.push_back({b,c});
}

// Build all skeleton lines (skip zero-length bones like pelvis)
static std::vector<LineVertex> buildSkeletonLines(const Skeleton& s){
    std::vector<LineVertex> v; v.reserve(s.bones.size()*2 + 200);
    const glm::vec3 boneColor(1.0f, 0.9f, 0.4f);

    for (size_t i = 0; i < s.bones.size(); ++i) {
        const auto& b = s.bones[i];
        // skip invisible or placeholder bones
        if (b.length <= 0.001f) continue; // pelvis root
        if (i == 0) continue;             // explicitly skip root bone
        if (i == 3) continue;             // skip head bone (sphere)
        glm::vec3 a = jointPos(b);
        glm::vec3 e = endpointPos(b);
        appendLine(v, a, e, boneColor);
    }    

    // Ground grid (XZ)
    const float G = 2.0f; int N = 20;
    for(int i=-N;i<=N;++i){
        float t = (i%5==0)? 0.2f:0.08f;
        glm::vec3 c(t,t,t);
        appendLine(v, { (float)i*0.1f, 0, -G }, {(float)i*0.1f, 0, +G}, c);
        appendLine(v, { -G, 0, (float)i*0.1f }, { +G, 0, (float)i*0.1f }, c);
    }
    return v;
}

// Build a UV-sphere (triangles) centered at the head center
static std::vector<TriVertex> buildHeadSphereTris(const Skeleton& s, int stacks=16, int slices=24){
    std::vector<TriVertex> tris;
    if (s.bones.size() <= 3) return tris;
    const glm::vec3 headColor(0.95f, 0.75f, 0.25f);

    const Bone& head = s.bones[3];

    // Neck joint = base of the head
    glm::vec3 neckBase = jointPos(head);

    // Sphere radius proportional to head bone length
    float radius = head.length * 0.6f;

    // "Up" direction of the head in world space (its +Y axis)
    glm::vec3 up = glm::normalize(glm::vec3(head.global * glm::vec4(0,1,0,0)));

    // Place the sphere so its bottom touches the neck base:
    // center = neckBase + up * radius
    glm::vec3 center = neckBase + up * radius;

    // UV-sphere triangulation
    std::vector<glm::vec3> ring((size_t)(slices+1));
    std::vector<glm::vec3> prev;
    for(int i=0;i<=stacks;i++){
        float v = (float)i / (float)stacks;         // [0,1]
        float phi = v * glm::pi<float>();           // [0,pi]
        float sy = std::cos(phi);                   // y on unit sphere
        float sr = std::sin(phi);                   // ring radius
        for(int j=0;j<=slices;j++){
            float u = (float)j / (float)slices;     // [0,1]
            float theta = u * glm::two_pi<float>(); // [0,2pi]
            float sx = sr * std::cos(theta);
            float sz = sr * std::sin(theta);
            // Build in the head's local axes: use head.global's basis
            glm::vec3 X = glm::normalize(glm::vec3(head.global * glm::vec4(1,0,0,0)));
            glm::vec3 Y = up; // already normalized
            glm::vec3 Z = glm::normalize(glm::vec3(head.global * glm::vec4(0,0,1,0)));
            ring[(size_t)j] = center + radius*(sx*X + sy*Y + sz*Z);
        }
        if(i==0){ prev = ring; continue; }
        for(int j=0;j<slices;j++){
            glm::vec3 p00 = prev[(size_t)j];
            glm::vec3 p01 = prev[(size_t)(j+1)];
            glm::vec3 p10 = ring[(size_t)j];
            glm::vec3 p11 = ring[(size_t)(j+1)];

            tris.push_back({p00, headColor});
            tris.push_back({p10, headColor});
            tris.push_back({p11, headColor});

            tris.push_back({p00, headColor});
            tris.push_back({p11, headColor});
            tris.push_back({p01, headColor});
        }
        prev = ring;
    }
    return tris;
}

// ------------------------------------------------------------
// Animation
// ------------------------------------------------------------
static void animateWalk(Skeleton& s, float t){
    float walkSpeed = 1.6f; // steps per second
    float phase = t * walkSpeed * glm::two_pi<float>();

    auto setR = [&](int idx, float rx, float ry, float rz){ s.bones[idx].eulerDeg = {rx, ry, rz}; };

    const int root=0, spine=1, neck=2, head=3;
    int hipL=4, kneeL=5, ankleL=6; int hipR=7, kneeR=8, ankleR=9;
    int shoulderL=10, elbowL=11, wristL=12; int shoulderR=13, elbowR=14, wristR=15;

    setR(root, 0.0f, 0.0f, 3.0f * std::sin(phase*0.5f));

    setR(spine, 5.0f*std::sin(phase*0.5f), 0.0f, 0.0f);
    setR(neck, -3.0f*std::sin(phase*0.5f), 0.0f, 0.0f);
    setR(head, 2.0f*std::sin(phase*0.5f), 0.0f, 0.0f);

    float hipSwing = 30.0f * std::sin(phase);
    float kneeFlex = 25.0f * std::max(0.0f, std::sin(phase));

    setR(hipL,  hipSwing,  0, 0);
    setR(kneeL, -kneeFlex, 0, 0);
    setR(ankleL, 5.0f*std::sin(phase+0.4f), 0, 0);

    setR(hipR, -hipSwing,  0, 0);
    setR(kneeR, -25.0f * std::max(0.0f, std::sin(phase+glm::pi<float>())), 0, 0);
    setR(ankleR, 5.0f*std::sin(phase+glm::pi<float>()+0.4f), 0, 0);

    float armSwing = 35.0f * std::sin(phase + glm::pi<float>());
    float elbowFlex = 10.0f * std::max(0.0f, std::sin(phase + glm::pi<float>()));

    setR(shoulderL,  armSwing, 0, 0);
    setR(elbowL,    -elbowFlex,0,0);
    setR(wristL,     5.0f*std::sin(phase+1.0f),0,0);

    setR(shoulderR, -armSwing, 0, 0);
    setR(elbowR,    -10.0f * std::max(0.0f, std::sin(phase)), 0, 0);
    setR(wristR,     5.0f*std::sin(phase+glm::pi<float>()+1.0f),0,0);

    s.updateGlobals();
}

// ------------------------------------------------------------
// Camera & input
// ------------------------------------------------------------
struct Camera {
    float yaw = 30.0f, pitch = -15.0f, dist = 3.0f;
    glm::vec3 target = {0, 1.0f, 0};

    glm::mat4 view() const {
        float cy = std::cos(glm::radians(yaw));
        float sy = std::sin(glm::radians(yaw));
        float cp = std::cos(glm::radians(pitch));
        float sp = std::sin(glm::radians(pitch));
        glm::vec3 dir = {cy*cp, sp, sy*cp};
        glm::vec3 eye = target - dir * dist;
        return glm::lookAt(eye, target, {0,1,0});
    }
};

static bool g_mouseDown=false; static double g_lastX=0, g_lastY=0; static Camera g_cam;
static void cursorPos(GLFWwindow*, double x, double y){ if(!g_mouseDown){ g_lastX=x; g_lastY=y; return; } double dx=x-g_lastX, dy=y-g_lastY; g_lastX=x; g_lastY=y; g_cam.yaw += (float)dx*0.3f; g_cam.pitch += (float)dy*0.3f; g_cam.pitch = glm::clamp(g_cam.pitch, -85.0f, 85.0f);} 
static void mouseBtn(GLFWwindow*, int button, int action, int){ if(button==GLFW_MOUSE_BUTTON_LEFT) g_mouseDown = (action==GLFW_PRESS); }
static void scrollCB(GLFWwindow*, double, double yoff){ g_cam.dist = glm::clamp(g_cam.dist - (float)yoff*0.2f, 1.2f, 8.0f); }

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main(){
    if(!glfwInit()){ std::fprintf(stderr, "Failed to init GLFW\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "OpenGL3 Skeleton Walk (Head Sphere)", nullptr, nullptr);
    if(!win){ std::fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)){
        std::fprintf(stderr, "Failed to init GLAD\n"); return 1; }

    glfwSetCursorPosCallback(win, cursorPos); glfwSetMouseButtonCallback(win, mouseBtn); glfwSetScrollCallback(win, scrollCB);

    GLuint prog = makeProgram();
    GLint uView = glGetUniformLocation(prog, "uView");
    GLint uProj = glGetUniformLocation(prog, "uProj");

    // --- VAO/VBO for lines (skeleton + grid)
    GLuint vaoLines=0, vboLines=0; glGenVertexArrays(1, &vaoLines); glGenBuffers(1, &vboLines);
    glBindVertexArray(vaoLines);
    glBindBuffer(GL_ARRAY_BUFFER, vboLines);
    glBufferData(GL_ARRAY_BUFFER, 1024*1024, nullptr, GL_DYNAMIC_DRAW); // 1 MB
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)(sizeof(glm::vec3)));
    glBindVertexArray(0);

    // --- VAO/VBO for triangles (head sphere)
    GLuint vaoTris=0, vboTris=0; glGenVertexArrays(1, &vaoTris); glGenBuffers(1, &vboTris);
    glBindVertexArray(vaoTris);
    glBindBuffer(GL_ARRAY_BUFFER, vboTris);
    glBufferData(GL_ARRAY_BUFFER, 1024*1024, nullptr, GL_DYNAMIC_DRAW); // 1 MB
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TriVertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TriVertex), (void*)(sizeof(glm::vec3)));
    glBindVertexArray(0);

    Skeleton skel = makeHuman();

    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_CULL_FACE); glCullFace(GL_BACK); // optional: cull back-faces

    double start = glfwGetTime();

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0,0,w,h);
        glClearColor(0.05f,0.06f,0.08f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        float t = (float)(glfwGetTime() - start);
        animateWalk(skel, t);

        // Build/draw lines
        std::vector<LineVertex> lineVerts = buildSkeletonLines(skel);
        glBindBuffer(GL_ARRAY_BUFFER, vboLines);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(lineVerts.size()*sizeof(LineVertex)), lineVerts.data());

        // Build/draw head sphere
        std::vector<TriVertex> triVerts = buildHeadSphereTris(skel, /*stacks=*/16, /*slices=*/24);
        glBindBuffer(GL_ARRAY_BUFFER, vboTris);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(triVerts.size()*sizeof(TriVertex)), triVerts.data());

        glm::mat4 V = g_cam.view();
        glm::mat4 P = glm::perspective(glm::radians(60.0f), w>0? (float)w/(float)h : 1.6f, 0.05f, 50.0f);

        glUseProgram(prog);
        glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(V));
        glUniformMatrix4fv(uProj, 1, GL_FALSE, glm::value_ptr(P));

        // Draw triangles (head) first or last — either is fine with depth test
        glBindVertexArray(vaoTris);
        glDrawArrays(GL_TRIANGLES, 0, (GLint)triVerts.size());
        glBindVertexArray(0);

        glBindVertexArray(vaoLines);
        glDrawArrays(GL_LINES, 0, (GLint)lineVerts.size());
        glBindVertexArray(0);

        glfwSwapBuffers(win);
    }

    glDeleteBuffers(1, &vboLines);
    glDeleteVertexArrays(1, &vaoLines);
    glDeleteBuffers(1, &vboTris);
    glDeleteVertexArrays(1, &vaoTris);
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
