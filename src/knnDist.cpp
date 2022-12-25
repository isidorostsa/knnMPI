#include <iostream>

#include <algorithm>
#include <numeric> 
#include <limits>

#include <cblas.h>

#include "global_vars.hpp"
#include "knnDist.hpp"

#define idx(i, j, d) ((i)*(d) + (j))

CorpusPacket::CorpusPacket(
    size_t n_packet, size_t d,
    size_t y_start_index, size_t y_end_index
) : 
    n_packet(n_packet), d(d),
    y_start_index(y_start_index), y_end_index(y_end_index)
{
    Y = std::vector<double>(n_packet * d);
}

CorpusPacket::CorpusPacket(
    size_t n_packet, size_t d,
    size_t y_start_index, size_t y_end_index,
    std::vector<double>&& Y
) : 
    n_packet(n_packet), d(d),
    y_start_index(y_start_index), y_end_index(y_end_index),
    Y(std::move(Y))
{
}

QueryPacket::QueryPacket(
    size_t m_packet, size_t d,
    size_t x_start_index, size_t x_end_index
) : 
    m_packet(m_packet), d(d),
    x_start_index(x_start_index), x_end_index(x_end_index) 
{
    X = std::vector<double>(m_packet * d);
}

QueryPacket::QueryPacket(
    size_t m_packet, size_t d,
    size_t x_start_index, size_t x_end_index,
    std::vector<double>&& X
) : 
    m_packet(m_packet), d(d),
    x_start_index(x_start_index), x_end_index(x_end_index),
    X(std::move(X))
{
}

ResultPacket::ResultPacket(
    size_t m_packet, size_t n_packet, size_t k_arg,
    size_t x_start_index, size_t x_end_index,
    size_t y_start_index, size_t y_end_index
) :
    m_packet(m_packet), n_packet(n_packet), k(std::min(k_arg, n_packet)),
    x_start_index(x_start_index), x_end_index(x_end_index),
    y_start_index(y_start_index), y_end_index(y_end_index)
{
    nidx = std::vector<size_t>(m_packet * this->k);
    ndist = std::vector<double>(m_packet * this->k);
}

ResultPacket::ResultPacket(
    const QueryPacket& query,
    const CorpusPacket& corpus,
    size_t k_arg
) :
    m_packet(query.m_packet), n_packet(corpus.n_packet), k(std::min(k_arg, corpus.n_packet)),
    x_start_index(query.x_start_index), x_end_index(query.x_end_index),
    y_start_index(corpus.y_start_index), y_end_index(corpus.y_end_index)
{
    const size_t d = query.d;

    std::vector<double> X2(m_packet);
    for(size_t i = 0; i < m_packet; i++){
        X2[i] = cblas_ddot(
            d,
            &query.X[idx(i, 0, query.d)], 1,
            &query.X[idx(i, 0, query.d)], 1
        );
    }

    std::vector<double> Y2(corpus.n_packet);
    for(size_t i = 0; i < corpus.n_packet; i++){
        Y2[i] = cblas_ddot(
            d,
            &corpus.Y[idx(i, 0, corpus.d)], 1,
            &corpus.Y[idx(i, 0, corpus.d)], 1
        );
    }

    std::vector<double> D(query.m_packet * corpus.n_packet);

/* ALTERNATIVELY POSSIBLY FASTER
    cblas_dgemm(
        CblasRowMajor, CblasNoTrans, CblasTrans,
        query.m_packet, corpus.n_packet, corpus.d,
        -2.0,
        query.X.data(), query.d,
        corpus.Y.data(), corpus.d,
        0.0,
        D.data(), corpus.n_packet
    );

    for(size_t i = 0; i < query.m_packet; i++){
        // maybe cache X2_i
        // X2_i = X2[i];
        for(size_t j = 0; j < corpus.n_packet; j++){
            D[idx(i, j, corpus.n_packet)] += X2[i] + Y2[j];
        }
    }

*/
    for(size_t i = 0; i < query.m_packet; i++){
        // maybe cache X2_i
        // X2_i = X2[i];
        for(size_t j = 0; j < corpus.n_packet; j++){
        D[idx(i, j, corpus.n_packet)] = X2[i] + Y2[j];
        }
    }

    cblas_dgemm(
        CblasRowMajor, CblasNoTrans, CblasTrans,
        query.m_packet, corpus.n_packet, corpus.d,
        -2.0,
        query.X.data(), query.d,
        corpus.Y.data(), corpus.d,
        1.0,
        D.data(), corpus.n_packet
    );

    nidx.resize(query.m_packet * k);
    ndist.resize(query.m_packet * k);

    for(size_t i = 0; i < query.m_packet; i++) {
        std::vector<size_t> indices(corpus.n_packet);
        std::iota(indices.begin(), indices.end(), 0);

        size_t n_packet = corpus.n_packet;

        std::partial_sort_copy(
            indices.begin(), indices.end(),
            &nidx[idx(i, 0, k)], &nidx[idx(i, k, k)],
            [&D, i, n_packet](size_t a, size_t b) {
                return D[idx(i, a, n_packet)] < D[idx(i, b, n_packet)]; 
            }
        );

        // convert y index to global y index
        for(size_t j = 0; j < k; j++) {
            nidx[idx(i, j, k)] += corpus.y_start_index;
        }

        for(size_t j = 0; j < k; j++) {
            size_t jth_nn = nidx[idx(i, j, k)] - corpus.y_start_index;

            ndist[idx(i, j, k)] = D[idx(i, jth_nn, corpus.n_packet)];
        }
    }

}

// they need to be distances of
// SAME query points
// DIFFERENT but CONTIGUOUS or overlaping corpus points
// Assuming back is ***cyclically*** behind front
// Meaning back could be [400:600] and front [0:100] if 600 is the end of y points
std::tuple<bool, size_t, size_t> ResultPacket::combinableSameX(const ResultPacket& back, const ResultPacket& front) {
    const bool combinable = (
        back.k == front.k &&
        back.m_packet == front.m_packet &&
        back.x_start_index == front.x_start_index &&
        back.x_end_index == front.x_end_index
    );

    if (combinable) {
        return std::tuple(true, back.y_start_index, front.y_end_index);
    }

    return std::tuple(false, -1, -1);
}

// they need to be distances of
// DIFFERENT but CONTIGUOUS query points
// SAME corpus points  
std::tuple<bool, size_t, size_t> ResultPacket::combinableSameY(ResultPacket const& back, ResultPacket const& front) {
    bool combinable = (
        back.k == front.k &&
        back.y_start_index == front.y_start_index && 
        back.y_end_index == front.y_end_index
    );

    if (combinable) {
        return std::tuple(true, back.x_start_index, front.x_end_index);
    }

    return std::tuple(false, -1, -1);
}


// it is assumed that the p1.y_end_index == p2.y_start_index
// for example we combine the k nearest neighbors of x[0:100] in both results
// but the first is the k nearest neighbors from y[0:100] and the second is the k nearest neighbors from y[100:200]
// another case is if we have y[600:700], (where 700 == end) and y[0:100]. 
// The resulting packet will have y_start_index = 600 and y_end_index = 100 (because it wraps around)
ResultPacket ResultPacket::combineKnnResultsSameX(const ResultPacket& back, const ResultPacket& front) {
    const auto [combinable, res_y_start_index, res_y_end_index] = ResultPacket::combinableSameX(back, front);
    if(!combinable) {
        throw std::runtime_error("Cannot combine knn results");
    }

    ResultPacket result(
        back.m_packet, back.n_packet + front.n_packet, std::max(back.k, front.k),
        back.x_start_index, back.x_end_index,
        res_y_start_index, res_y_end_index
    );

    for(size_t i = 0; i < result.m_packet; i++) {
        size_t b_idx = 0, f_idx = 0;

        while(b_idx + f_idx < result.k) {
            double l_dist = 
                b_idx == back.n_packet ? 
                std::numeric_limits<double>::max() : back.ndist[idx(i, b_idx, back.k)];

            double r_dist = 
                f_idx == front.n_packet ? 
                std::numeric_limits<double>::max() : front.ndist[idx(i, f_idx, front.k)];

            if (l_dist < r_dist) {
                result.ndist[idx(i, b_idx + f_idx, result.k)] = l_dist;
                result.nidx[idx(i, b_idx + f_idx, result.k)] = back.nidx[idx(i, b_idx, back.k)];
                b_idx++;
            } else {
                result.ndist[idx(i, b_idx + f_idx, result.k)] = r_dist;
                result.nidx[idx(i, b_idx + f_idx, result.k)] = front.nidx[idx(i, f_idx, front.k)];
                f_idx++;
            }
        }
    }

    return result;
}

/*
// it is assumed that p1.x_end_index == p2.x_start_index
// for example we combine the k nearest neighbors from y[0:100] in both results
// the first is the k nearest neighbors of x[0:100] and the second is the k nearest neighbors of x[100:200]
ResultPacket ResultPacket::combineKnnResultsSameY(const ResultPacket& p1, const ResultPacket& p2) {
    if(p1.y_end_index != p2.y_start_index) {
        throw std::runtime_error("Cannot combine knn results, y indices are not contiguous");
    }

    if(!ResultPacket::combinableSameY(p1, p2)) {
        throw std::runtime_error("Cannot combine knn results");
    }

    ResultPacket result(
        p1.m_packet + p2.m_packet, p1.n_packet, p1.k,
        p1.x_start_index, p2.x_end_index,
        p1.y_start_index, p1.y_end_index
    );

    for(size_t i = 0; i < p1.m_packet; i++) {
        for(size_t j = 0; j < p1.k; j++) {
            result.ndist[idx(i, j, p1.k)] = p1.ndist[idx(i, j, p1.k)];
            result.nidx[idx(i, j, p1.k)] = p1.nidx[idx(i, j, p1.k)];
        }
    }

    for(size_t i = 0; i < p2.m_packet; i++) {
        for(size_t j = 0; j < p2.k; j++) {
            result.ndist[idx(i + p1.m_packet, j, p2.k)] = p2.ndist[idx(i, j, p2.k)];
            result.nidx[idx(i + p1.m_packet, j, p2.k)] = p2.nidx[idx(i, j, p2.k)];
        }
    }

    return result;
} // maybe move one of the ndist and nidx vectors to the other one
*/

// they all share the same Y (which is the whole Y) and collectivly cover the whole X
static ResultPacket combineCompleteQueries(std::vector<ResultPacket> results) {
    std::vector<size_t> order(results.size());
    std::iota(order.begin(), order.end(), 0);

    std::sort(
        order.begin(), order.end(),
        [&results](size_t a, size_t b) {
        return results[a].x_start_index < results[b].x_start_index;
    });

    // check if everything is okay
    for(size_t i = 0, current_end = 0; i < order.size() - 1; i++) {
        const auto [combinable, y_start_index, y_end_index] =
            ResultPacket::combinableSameY(results[order[i]], results[order[i+1]]);

        if(!combinable) {
            throw std::runtime_error("Results are not combinable");
        }  
    }

    // m = the sum of all m
    size_t m = 0;
    for(auto& rp: results) {
        m += rp.m_packet;
    }

// CHECK CASE WHERE K IS SMALL FOR EDGE CASES

    // k = the k of any result
    size_t k = results[0].k;

    // y_end_index = the y_end_index of any result
    size_t y_end_index = results[0].y_end_index;

    ResultPacket result(m, results[0].n_packet, k, 0, m, 0, y_end_index);

    for(auto& rp: results) {
        if(rp.k != k) {
            throw std::runtime_error("Cannot combine knn results, k is not the same");
        }

        for(size_t i = rp.x_start_index; i < rp.x_end_index; i++) {
            for(size_t j = 0; j < rp.k; j++) {
                result.ndist[idx(i, j, rp.k)] = rp.ndist[idx(i, j, rp.k)];
                result.nidx[idx(i, j, rp.k)] = rp.nidx[idx(i, j, rp.k)];
            }
        }
    }

    size_t m_offset = 0;
    for(size_t packetNumber = 0; packetNumber < order.size(); packetNumber++) {
        size_t m_packet = results[order[packetNumber]].m_packet;

        for(size_t i = 0; i < m_packet; i++) {
            for(size_t j = 0; j < k; j++) {
                result.nidx[idx(m_offset + i, j, k)] = results[order[packetNumber]].nidx[idx(i, j, k)];
            }
        }

        for(size_t i = 0; i < m_packet; i++) {
            for(size_t j = 0; j < k; j++) {
                result.nidx[idx(m_offset + i, j, k)] = results[order[packetNumber]].nidx[idx(i, j, k)];
            }
        }

        // free up memory as you go
        results[order[packetNumber]].nidx = std::vector<size_t>(0);
        results[order[packetNumber]].ndist = std::vector<double>(0);

        m_offset += m_packet;
    }
}

// TODO 
// COMBINE KNN WITH Y[start:a] AND Y[b:end] (maybe)
// CHECK CASES WHERE K IS SMALL FOR EDGE CASES or MAKE SURE THAT K IS ALWAYS BIG ENOUGH