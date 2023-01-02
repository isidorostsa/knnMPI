#include <iostream>
#include <string>
#include <vector>

#include "detail/mpi_process.hpp"
#include "detail/communication.hpp"
#include "detail/knn_structs.hpp"
#include "detail/worker.hpp"
#include "detail/utilities.hpp"

#define MASTER_RANK 0

std::string filename;
size_t n_total; // # total points
size_t d;       // # dimensions
size_t k;       // # nearest neighbours

void print_results(const QueryPacket &query, const CorpusPacket &corpus, const ResultPacket &result, size_t k)
{
    // Print the results
    for (size_t i = 0; i < std::min(result.m_packet, (size_t)1); i++)
    {
        std::cout << "Nearest neighbors of point ";
        for (size_t j = 0; j < query.d; j++)
        {
            // std::cout << query.X[idx(i, j, query.d)] << " ";
        }
        std::cout << "are:" << std::endl;

        for (size_t j = 0; j < std::min(result.k, (size_t)1); j++)
        {
            double diff = 0;
            for (size_t comp = 0; comp < query.d; comp++)
            {
                diff += std::abs(query.X[idx(i, comp, query.d)] - corpus.Y[idx(result.nidx[idx(i, j, result.k)], comp, corpus.d)]);
                // std::cout << corpus.Y[idx(result.nidx[idx(i, j, result.k)],comp , corpus.d)] << " ";
            }
            std::cout << std::endl;
            std::cout << "diff: " << diff << std::endl;
        }
        std::cout << std::endl;
    }
}

// Entry point for MPI master
void master_main(mpi_process &process)
{
    com_port com(process.world_rank, process.world_size);

    std::vector<initial_work_data> init_data;
    size_t max_size = 1 + n_total / process.world_size;
    // Send initial data to all workers
    for (size_t i = 0; i < (size_t)process.world_size; i++)
    {
        size_t idx_start = (i * n_total) / process.world_size; // will this overflow?
        size_t idx_end = ((i + 1) * n_total) / process.world_size;

        init_data.push_back(initial_work_data(filename, idx_start, idx_end, max_size, d, k));

        std::cout << filename << " " << idx_start << " " << idx_end << std::endl;

        // send to everyone except this
        if (i != MASTER_RANK)
            com.send(i, init_data[i]);
    }
    // Initialize local worker
    worker w(process.world_rank, process.world_size, init_data[0]);

    w.work();

    // Gather result
    std::vector<ResultPacket> diffProcRes;
    diffProcRes.push_back(w.results);
    for (int i = 1; i < process.world_size; i++)
    {
        size_t query_size = (init_data[i].idx_end - init_data[i].idx_start);
        ResultPacket result(query_size * k);
        com.receive(i, result);
        diffProcRes.push_back(result);
    }

    std::cout << "RESULTS\n";
    for (auto &elem : diffProcRes)
    {
        std::cout << elem << std::endl;
    }

    ResultPacket final_result = combineCompleteQueries(diffProcRes);
    
    // free the memory
    for(auto& elem: diffProcRes) {
        elem.ndist = std::vector<double>();
        elem.nidx = std::vector<size_t>();
    }

    std::cout << "FINAL RESULTS\n";
    std::cout << final_result << std::endl;

    auto [query, corpus] = file_packets(filename, 0, n_total, d);
    std::cout << "Loaded query and corpus to print" << std::endl;
    print_results(query, corpus, final_result, k);
}

void slave_main(mpi_process &process)
{
    worker w(process.world_rank, process.world_size);
    w.work();
}

// MPI entry:
int main(int argc, char **argv)
{

    if (argc == 5)
    {
        filename = argv[1];
        n_total = std::stoi(argv[2]);
        d = std::stoi(argv[3]);
        k = std::stoi(argv[4]);

        std::cout << "filename: " << filename << std::endl;
        std::cout << "n_total: " << n_total << std::endl;
        std::cout << "d: " << d << std::endl;
        std::cout << "k: " << k << std::endl;
    }

    mpi_process process(&argc, &argv);
    if (argc != 5)
    {
        if (process.world_rank == MASTER_RANK)
        {
            std::cout << "Usage: " << argv[0] << " <filename> <n> <d> <k>" << std::endl;
        }
        return 1;
    }
    if (process.world_rank == MASTER_RANK)
    {

        utilities::timer my_timer;
        my_timer.start();
        master_main(process);
        my_timer.stop();
        std::cout << "Total time: " << my_timer.get() / 1000000.0 << " ms" << std::endl;
    }
    else
    {

        slave_main(process);
    }

    return 0;
}