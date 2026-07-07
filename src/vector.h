#pragma once

struct Vec2 {
    union {
        struct {
            float x;
            float y;
        };
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }
};

struct Vec3 {
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
        struct {
            float p;
            float y;
            float r;
        };
        Vec2 xy;
    };

    const float& operator[](size_t index) const {
        return (&x)[index];
    }
};

struct Vec4 {
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
        Vec2 xy;
        Vec3 xyz;   
    };
    
    const float& operator[](size_t index) const {
        return (&x)[index];
    }
};
