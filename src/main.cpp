#include <iostream>
#include <algorithm>
#include <vector>
#include <chrono>
#include <string>
#include <random>

#include "global_vars.hpp"
#include "knnDist.hpp"
#include "testingknn.hpp"
#include "fileio.hpp"

#define idx(i, j, ld) (((i)*(ld))+(j))
void print_results(const QueryPacket& query, const CorpusPacket& corpus, size_t k) {

    ResultPacket result = runData(query, corpus, k);

    // Print the results
    for (size_t i = 0; i < std::min(result.m_packet, (size_t)5); i++)
    {
        std::cout << "Nearest neighbors of point ";
        for (size_t j = 0; j < query.d; j++)
        {
            //std::cout << query.X[idx(i, j, query.d)] << " ";
        }
        std::cout << "are:" << std::endl;

        for (size_t j = 0; j < std::min(result.k, (size_t)3); j++)
        {
            double diff = 0;
            for(size_t comp = 0; comp < query.d; comp++){
                diff += std::abs(query.X[idx(i, comp, query.d)] - corpus.Y[idx(result.nidx[idx(i, j, result.k)],comp , corpus.d)]);
                //std::cout << corpus.Y[idx(result.nidx[idx(i, j, result.k)],comp , corpus.d)] << " ";
            }
            std::cout << std::endl;
            std::cout << "diff: " << diff << std::endl;
        }
        std::cout << std::endl;
    }
}


int main(int argc, char** argv)
{
    // Sample input values
/*
    size_t d = 28*28;

    std::vector<double> X = load_csv<double>("../mnist_test.csv", std::stoi(argv[1]));
    size_t m = X.size()/d;
    std::cout << "Loaded " << m << " points from mnist_test.csv" << std::endl;

    std::vector<double> Y = load_csv<double>("../mnist_train.csv", std::stoi(argv[2]));
    size_t n = Y.size()/d;
    std::cout << "Loaded " << n << " points from mnist_train.csv" << std::endl;

    size_t k = 1;
*/
    // if first arg is 'regular' then use regular grid
    if(std::string(argv[1]) == "regular") {
        size_t s = std::stoi(argv[2]);
        size_t d = std::stoi(argv[3]);
        size_t m = std::stoi(argv[4]);

        bool debug = std::stoi(argv[6]) == 1;

        const auto [query, corpus, k] = regual_grid(s, d, m);

        print_results(query, corpus, k);
    } else if (std::string(argv[1]) == "random") {
        size_t m = std::stoi(argv[2]);
        size_t n = std::stoi(argv[3]);
        size_t d = std::stoi(argv[4]);
        size_t k = std::stoi(argv[5]);

        debug = std::stoi(argv[6]) == 1;

        // set the query and corpus objects to the result of random_grid
        const auto [query, corpus] = random_grid(m, n, d, k);

        print_results(query, corpus, k);
    } else {
        std::cout << "Invalid input" << std::endl;
        return 1;
    }

    return 0;
}