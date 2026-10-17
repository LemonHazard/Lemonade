#pragma once

struct Vector2 {
    union {
        struct {
            float x;
            float y;
        };
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x; 
    }
};

struct Vector3 {
    union {
        struct {
            float x;
            float y;
            float z;
        };
        struct {
            float r;
            float g;
            float b;
        };
        Vector2 xy;
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x;
    }
};

struct Vector4 {
    union {
        struct {
            float x;
            float y;
            float z;
            float w;
        };
        struct {
            float r;
            float g;
            float b;
            float a;
        };
        Vector2 xy;
        Vector3 xyz;
    };
    
    const float& operator[](size_t index) const {
        return (&x)[index];
    }

    operator const float*() const {
        return &x; 
    }
};

struct Rotation3 {
    union {
        struct {
            float pitch;
            float yaw;
            float roll;
        };
        struct {
            float p;
            float y;
            float r;
        };
    };

    const float& operator[](size_t index) const {
        return (&pitch)[index];
    }

    operator const float*() const {
        return &pitch;
    }
};
