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
        struct {
            float p;
            float y;
            float r;
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
