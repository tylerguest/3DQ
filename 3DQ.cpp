// compile: g++ 3DQ.cpp -o 3dq -lglfw -lGL -lGLU -lm

#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <cmath>
#include <vector>

const int WIDTH = 1200;
const int HEIGHT = 1000;
const int NUM_POINTS = 1000; // Number of points on the sphere

float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
bool mousePressed = false;
float yaw = 0.0f;
float pitch = 0.0f;

std::vector<float> spherePoints; // Stores the coordinates of points on the sphere

void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (mousePressed) {
        float xOffset = xpos - lastX;
        float yOffset = lastY - ypos;
        lastX = xpos;
        lastY = ypos;

        const float sensitivity = 0.1f;
        xOffset *= sensitivity;
        yOffset *= sensitivity;

        yaw += xOffset;
        pitch += yOffset;

        // Limit pitch to prevent flipping
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mousePressed = true;
            glfwGetCursorPos(window, reinterpret_cast<double*>(&lastX), reinterpret_cast<double*>(&lastY));
        } else if (action == GLFW_RELEASE) {
            mousePressed = false;
        }
    }
}

void generateSpherePoints() {
    for (int i = 0; i < NUM_POINTS; ++i) {
        float u = float(rand()) / RAND_MAX;
        float v = float(rand()) / RAND_MAX;
        float theta = 2 * M_PI * u;
        float phi = acos(2 * v - 1);
        
        float x = sin(phi) * cos(theta);
        float y = sin(phi) * sin(theta);
        float z = cos(phi);
        
        spherePoints.push_back(x);
        spherePoints.push_back(y);
        spherePoints.push_back(z);
    }
}

void drawSphereWithDots() {
    const float dotSize = 4.0f; // Size of the dots
    const float dotColor[3] = {0.0f, 1.0f, 0.0f}; // Color of the dots (green)

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Render the sphere filled with the desired opacity
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 1.0f, 1.0f, 0.2f); // Set sphere fill color to white with 20% opacity
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    GLUquadric* quad = gluNewQuadric();
    gluSphere(quad, 1.0f, 30, 30); // Using constants directly for slices and stacks
    gluDeleteQuadric(quad);
    glDisable(GL_BLEND);

    // Render dots on the sphere's surface
    glColor3fv(dotColor); // Set dot color to green
    glPointSize(dotSize);
    glBegin(GL_POINTS);
    for (int i = 0; i < NUM_POINTS; ++i) {
        // Transform the dot's coordinates with the sphere's rotation
        float x = spherePoints[i * 3];
        float y = spherePoints[i * 3 + 1];
        float z = spherePoints[i * 3 + 2];

        // Apply the same rotation to the dot as to the sphere
        float newX = x * cos(pitch * M_PI / 180) + z * sin(pitch * M_PI / 180);
        float newY = y;
        float newZ = -x * sin(pitch * M_PI / 180) + z * cos(pitch * M_PI / 180);

        float finalX = newX * cos(yaw * M_PI / 180) - newY * sin(yaw * M_PI / 180);
        float finalY = newX * sin(yaw * M_PI / 180) + newY * cos(yaw * M_PI / 180);
        float finalZ = newZ;

        glVertex3f(finalX, finalY, finalZ); // Render dot at transformed (x, y, z)
    }
    glEnd();

    // Disable depth testing after rendering
    glDisable(GL_DEPTH_TEST);
}




int main() {
    if (!glfwInit()) {
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "3D Sphere", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    //glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); // Enable blending for transparency
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Set blending function

    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (float)WIDTH / (float)HEIGHT, 0.1f, 100.0f);

    generateSpherePoints(); // Generate points on the sphere

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAt(0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

        drawSphereWithDots();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

