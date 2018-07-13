
#ifndef SIGPROC_DIRECTFORM2_H
#define SIGPROC_DIRECTFORM2_H

#include <deque>
#include <vector>

template <class T>
class DirectForm2Mono
{
public:
    DirectForm2Mono(std::vector<T> B, std::vector<T> A)
    : m_B(B), m_A(A), m_deque() {

        // TODO: assert m_A, m_B are equal size
        m_N = m_B.size();
        //in director form 2 the multiplicative constant
        // for feedback is negative the pole value
        for (int i=0; i<m_N; i++) {
            m_A[i] = -m_A[i];
        }
        m_A[0] = 1.0;

        //initialize the output
        for (int i=0; i<m_N; i++) {
            m_deque.push_back(0.0);
        }

    };

    DirectForm2Mono(T* B, T* A, int N)
    : m_B(), m_A(), m_deque() {

        m_N = N;
        //in director form 2 the multiplicative constant
        // for feedback is negative the pole value
        for (int i=0; i<m_N; i++) {
            m_B.push_back(B[i]);
            m_A.push_back(-A[i]);
        }
        m_A[0] = 1.0;

        //initialize the output
        for (int i=0; i<m_N; i++) {
            m_deque.push_back(0.0);
        }

    };

    ~DirectForm2Mono() {}

    T IIR(T value) {
        T a=0, b=0;

        for (int i=1; i<m_N; i++) {
            a += m_A[i] * m_deque[i-1];
            b += m_B[i] * m_deque[i-1];
        }

        //T new_value = value * m_A[0] + a
        T new_value = value + a;

        m_deque.pop_back();
        m_deque.push_front(new_value);

        return new_value * m_B[0] + b;
    }

private:
    std::vector<T> m_B;
    std::vector<T> m_A;
    std::deque<T> m_deque;
    size_t m_N;
};


#endif

