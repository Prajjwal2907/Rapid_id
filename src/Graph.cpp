#include "Graph.hpp"
#include "GeoMath.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace rapidaid::graph {

int Graph::addNode(long long osmId, double lat, double lon) {
    auto it = m_osmIdToIndex.find(osmId);
    if (it != m_osmIdToIndex.end()) {
        return it->second;
    }
    int newIndex = static_cast<int>(m_nodes.size());
    m_nodes.push_back({osmId, lat, lon, {}});
    m_osmIdToIndex[osmId] = newIndex;
    return newIndex;
}

void Graph::addEdge(int a, int b, double weightMeters) {
    if (weightMeters < 0.0) {
        return;
    }
    if (a == b) {
        return;
    }
    int count = static_cast<int>(m_nodes.size());
    if (a < 0 || a >= count || b < 0 || b >= count) {
        return;
    }

    // Check if edge already exists
    for (auto& edge : m_nodes[a].edges) {
        if (edge.to == b) {
            if (weightMeters < edge.weightMeters) {
                edge.weightMeters = weightMeters;
                for (auto& revEdge : m_nodes[b].edges) {
                    if (revEdge.to == a) {
                        revEdge.weightMeters = weightMeters;
                        break;
                    }
                }
            }
            return;
        }
    }

    // New undirected edge
    m_nodes[a].edges.push_back({b, weightMeters});
    m_nodes[b].edges.push_back({a, weightMeters});
    ++m_edgeCount;
}

int Graph::nodeCount() const {
    return static_cast<int>(m_nodes.size());
}

size_t Graph::edgeCount() const {
    return m_edgeCount;
}

const std::vector<Edge>& Graph::neighbors(int index) const {
    static const std::vector<Edge> kEmptyNeighbors;
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return kEmptyNeighbors;
    }
    return m_nodes[index].edges;
}

long long Graph::osmId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0;
    }
    return m_nodes[index].osmId;
}

double Graph::lat(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0.0;
    }
    return m_nodes[index].lat;
}

double Graph::lon(int index) const {
    if (index < 0 || index >= static_cast<int>(m_nodes.size())) {
        return 0.0;
    }
    return m_nodes[index].lon;
}

int Graph::indexOf(long long osmId) const {
    auto it = m_osmIdToIndex.find(osmId);
    if (it == m_osmIdToIndex.end()) {
        return -1;
    }
    return it->second;
}

int Graph::nearestNode(double lat, double lon, double* distanceMetersOut) const {
    if (m_nodes.empty()) {
        return -1;
    }

    int bestIndex = -1;
    double minDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        double d = rapidaid::geo::haversineMeters(lat, lon, m_nodes[i].lat, m_nodes[i].lon);
        if (d < minDistance) {
            minDistance = d;
            bestIndex = i;
        }
    }

    if (distanceMetersOut != nullptr) {
        *distanceMetersOut = minDistance;
    }
    return bestIndex;
}

PathResult Graph::shortestPath(int from, int to) const {
    int totalNodes = static_cast<int>(m_nodes.size());
    if (from < 0 || from >= totalNodes || to < 0 || to >= totalNodes) {
        return PathResult{false, 0.0, {}};
    }

    if (from == to) {
        return PathResult{true, 0.0, {from}};
    }

    const double kInfinity = std::numeric_limits<double>::infinity();
    std::vector<double> dist(totalNodes, kInfinity);
    std::vector<int> pred(totalNodes, -1);

    using QueueElement = std::pair<double, int>;
    std::priority_queue<QueueElement, std::vector<QueueElement>, std::greater<QueueElement>> pq;

    dist[from] = 0.0;
    pq.push({0.0, from});

    while (!pq.empty()) {
        auto [currentDist, u] = pq.top();
        pq.pop();

        // Skip stale queue entries
        if (currentDist > dist[u]) {
            continue;
        }

        if (u == to) {
            break;
        }

        for (const auto& edge : m_nodes[u].edges) {
            int v = edge.to;
            double nextDist = dist[u] + edge.weightMeters;
            if (nextDist < dist[v]) {
                dist[v] = nextDist;
                pred[v] = u;
                pq.push({nextDist, v});
            }
        }
    }

    if (dist[to] == kInfinity) {
        return PathResult{false, 0.0, {}};
    }

    std::vector<int> path;
    for (int curr = to; curr != -1; curr = pred[curr]) {
        path.push_back(curr);
    }
    std::reverse(path.begin(), path.end());

    return PathResult{true, dist[to], path};
}

Graph Graph::largestComponent() const {
    int count = static_cast<int>(m_nodes.size());
    if (count == 0) {
        return Graph();
    }

    std::vector<bool> visited(count, false);
    std::vector<int> bestComponent;

    for (int i = 0; i < count; ++i) {
        if (visited[i]) {
            continue;
        }

        std::vector<int> currentComponent;
        std::vector<int> queue;
        queue.push_back(i);
        visited[i] = true;
        size_t head = 0;

        while (head < queue.size()) {
            int u = queue[head++];
            currentComponent.push_back(u);

            for (const auto& edge : m_nodes[u].edges) {
                int v = edge.to;
                if (!visited[v]) {
                    visited[v] = true;
                    queue.push_back(v);
                }
            }
        }

        // We iterate i from 0 to count - 1.
        // Therefore, node i is the minimum index in currentComponent.
        // If currentComponent is strictly larger than bestComponent, it becomes bestComponent.
        // In case of a tie in size, the earlier component had a lower root index,
        // so strictly greater preserves the component with the lower index.
        if (currentComponent.size() > bestComponent.size()) {
            bestComponent = std::move(currentComponent);
        }
    }

    Graph componentGraph;
    if (bestComponent.empty()) {
        return componentGraph;
    }

    std::vector<int> oldToNew(count, -1);
    for (int oldIndex : bestComponent) {
        int newIndex = componentGraph.addNode(
            m_nodes[oldIndex].osmId,
            m_nodes[oldIndex].lat,
            m_nodes[oldIndex].lon
        );
        oldToNew[oldIndex] = newIndex;
    }

    for (int oldIndex : bestComponent) {
        int newU = oldToNew[oldIndex];
        for (const auto& edge : m_nodes[oldIndex].edges) {
            int oldV = edge.to;
            if (oldIndex < oldV) {
                int newV = oldToNew[oldV];
                componentGraph.addEdge(newU, newV, edge.weightMeters);
            }
        }
    }

    return componentGraph;
}

} // namespace rapidaid::graph
