#include "knn_utils.hpp"
#include "knn_structs.hpp"
#include "fileio.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

size_t idx(size_t i, size_t j, size_t ld)
{
    return i * ld + j;
}

// they need to be distances of
// SAME query points
// DIFFERENT but CONTIGUOUS or overlaping corpus points
// Assuming back is ***cyclically*** behind front
// Meaning back could be [400:600] and front [0:100] if 600 is the end of y
// points
std::tuple<bool, size_t, size_t> combinableSameX(const ResultPacket &back,
                                                 const ResultPacket &front)
{
    const bool combinable = (back.k == front.k && back.m_packet == front.m_packet && back.x_start_index == front.x_start_index && back.x_end_index == front.x_end_index);

    if (combinable)
    {
        return std::tuple(true, back.y_start_index, front.y_end_index);
    }

    return std::tuple(false, -1, -1);
}

// they need to be distances of
// DIFFERENT but CONTIGUOUS query points
// SAME corpus points
std::tuple<bool, size_t, size_t> combinableSameY(ResultPacket const &back,
                                                 ResultPacket const &front)
{
    bool combinable = ((back.k == front.k) && ((back.y_start_index == front.y_start_index && back.y_end_index == front.y_end_index) || (back.y_end_index - back.y_start_index == 0 || front.y_end_index - front.y_start_index == 0)));

    if (combinable)
    {
        return std::tuple(true, back.x_start_index, front.x_end_index);
    }

    return std::tuple(false, -1, -1);
}

// it is assumed that the p1.y_end_index == p2.y_start_index
// for example we combine the k nearest neighbors of x[0:100] in both results
// but the first is the k nearest neighbors from y[0:100] and the second is the
// k nearest neighbors from y[100:200] another case is if we have y[600:700],
// (where 700 == end) and y[0:100]. The resulting packet will have
// y_start_index = 600 and y_end_index = 100 (because it wraps around)
ResultPacket combineKnnResultsSameX(const ResultPacket &back,
                                    const ResultPacket &front)
{
    if (back.k == 0 && back.m_packet == 0)
        return front;

    const auto [combinable, res_y_start_index, res_y_end_index] = combinableSameX(back, front);
    if (!combinable)
    {
        throw std::runtime_error("Cannot combine knn results");
    }

    ResultPacket result(back.m_packet, back.n_packet + front.n_packet,
                        std::max(back.k, front.k), back.x_start_index,
                        back.x_end_index, res_y_start_index, res_y_end_index);

    for (size_t i = 0; i < result.m_packet; i++)
    {
        size_t b_idx = 0, f_idx = 0;

        while (b_idx + f_idx < result.k)
        {
            double l_dist = b_idx == back.n_packet
                                ? std::numeric_limits<double>::max()
                                : back.ndist[idx(i, b_idx, back.k)];

            double r_dist = f_idx == front.n_packet
                                ? std::numeric_limits<double>::max()
                                : front.ndist[idx(i, f_idx, front.k)];

            if (l_dist < r_dist)
            {
                result.ndist[idx(i, b_idx + f_idx, result.k)] = l_dist;
                result.nidx[idx(i, b_idx + f_idx, result.k)] = back.nidx[idx(i, b_idx, back.k)];
                b_idx++;
            }
            else
            {
                result.ndist[idx(i, b_idx + f_idx, result.k)] = r_dist;
                result.nidx[idx(i, b_idx + f_idx, result.k)] = front.nidx[idx(i, f_idx, front.k)];
                f_idx++;
            }
        }
    }

    return result;
}

// they all share the same Y (which is the whole Y) and collectivly cover the
// whole X
ResultPacket combineCompleteQueries(
    std::vector<ResultPacket> &completeResults)
{
    std::vector<size_t> order(completeResults.size());
    std::iota(order.begin(), order.end(), 0);

    // indices of elements in order
    std::sort(order.begin(), order.end(),
              [&completeResults](size_t a, size_t b)
              {
                  return completeResults[a].x_start_index < completeResults[b].x_start_index;
              });

    for (size_t i = 0; i < order.size() - 1; i++)
    {
        const auto &back = completeResults[order[i]];
        const auto &front = completeResults[order[i + 1]];

        if (back.x_end_index != front.x_start_index)
        {
            throw std::runtime_error("There are gaps in the results");
        }

        const auto [combinable, _, __] = combinableSameY(back, front);

        if (!combinable)
        {
            throw std::runtime_error("Results are not combinable");
        }
    }

    size_t m = 0;
    for (auto &rp : completeResults)
    {
        m += rp.m_packet;
    }

    // k = the k of any result
    size_t k = completeResults[0].k;

    // y_end_index = the y_end_index of any result
    size_t y_end_index = completeResults[0].y_end_index;

    ResultPacket result(m, completeResults[0].n_packet, k, 0, m, 0,
                        y_end_index);

    size_t m_offset = 0;
    for (size_t packet = 0; packet < completeResults.size(); packet++)
    {
        auto &rp = completeResults[order[packet]];

        for (size_t i = 0; i < rp.m_packet; i++)
            std::copy(&rp.ndist[idx(i, 0, k)], &rp.ndist[idx(i, k, k)],
                      &result.ndist[idx(i + m_offset, 0, k)]);
        for (size_t i = 0; i < rp.m_packet; i++)
            std::copy(&rp.nidx[idx(i, 0, k)], &rp.nidx[idx(i, k, k)],
                      &result.nidx[idx(i + m_offset, 0, k)]);

        rp.ndist = std::vector<double>();
        rp.nidx = std::vector<size_t>();

        m_offset += rp.m_packet;
    }

    return result;
}

// RowMajor 3d grid up to SxSxS
// m is query points
std::tuple<QueryPacket, CorpusPacket, size_t> regual_grid(size_t s, size_t d,
                                                          size_t m)
{
    // Corpus points
    size_t n = 1;
    for (size_t _ = 0; _ < d; _++)
    {
        n *= s;
    }

    std::vector<double> Y(n * d);

    for (size_t id = 0; id < n; id++)
    {
        size_t i = id;
        for (size_t comp = 0; comp < d; comp++)
        {
            Y[idx(id, comp, d)] = i % s;
            i /= s;
        }
    }

    // Query points, m is given
    std::vector<double> X(m * d);

    // random points in the grid
    for (size_t i = 0; i < m; i++)
    {
        const size_t y_choice = rand() % n;
        for (size_t comp = 0; comp < d; comp++)
            X[idx(i, comp, d)] = Y[idx(y_choice, comp, d)];
    }

    // 3**d is the imediate neighbors
    size_t k = 1;
    for (size_t i = 0; i < d; i++)
    {
        k *= 3;
    }

    return std::make_tuple(QueryPacket(m, d, 0, m, std::move(X)),
                           CorpusPacket(n, d, 0, n, std::move(Y)), k);
}

std::tuple<QueryPacket, CorpusPacket> random_grid(size_t m, size_t n, size_t d,
                                                  size_t k)
{
    size_t s = 1000;

    std::vector<double> Y(n * d);

    for (size_t id = 0; id < n; id++)
    {
        for (size_t comp = 0; comp < d; comp++)
        {
            Y[idx(id, comp, d)] = rand() % s;
        }
    }

    // Query points, m is given
    std::vector<double> X(m * d);

    // random points in the grid
    for (size_t i = 0; i < m; i++)
    {
        const size_t y_choice = rand() % n;
        for (size_t comp = 0; comp < d; comp++)
            X[idx(i, comp, d)] = Y[idx(y_choice, comp, d)];
    }

    return std::make_tuple(QueryPacket(m, d, 0, m, std::move(X)),
                           CorpusPacket(n, d, 0, n, std::move(Y)));
}

std::tuple<QueryPacket, CorpusPacket>
file_packets(const std::string &query_path, const size_t query_start_idx,  const size_t query_end_idx,
             const std::string &corpus_path, const size_t corpus_start_idx,  const size_t corpus_end_idx,
             const size_t d_upper_limit)
{

    auto [X, m, d] =
        load_csv<double>(query_path, query_start_idx, query_end_idx, d_upper_limit, false);

    auto [Y, n, d_ignore] =
        load_csv<double>(corpus_path, corpus_start_idx, corpus_end_idx, d_upper_limit, false);

    return std::make_tuple(QueryPacket(m, d, query_start_idx, query_end_idx, std::move(X)),
                           CorpusPacket(n, d, corpus_start_idx, corpus_end_idx, std::move(Y)));
}

// Use when query and corpus are the same
std::tuple<QueryPacket, CorpusPacket>
file_packets(const std::string &file_path, const size_t start_idx,  const size_t end_idx,
             const size_t d_upper_limit){
                return file_packets(file_path, start_idx, end_idx, file_path, start_idx, end_idx, d_upper_limit); 
             }

