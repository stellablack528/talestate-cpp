"""Map scale and route lengths determine walking time; no visual map required."""

from heapq import heappop, heappush
from math import ceil


class TravelMap:
    def __init__(self, definition):
        self.definition = definition
        self.routes = definition.get("routes", [])
        self.speed = definition.get("walking_speed_kmh", 4)

    def neighbors(self, location):
        for route in self.routes:
            a, b = route["from"], route["to"]
            if a == location:
                yield b, route["distance_km"]
            elif b == location:
                yield a, route["distance_km"]

    def distance(self, start, destination):
        queue, best = [(0, start)], {start: 0}
        while queue:
            distance, node = heappop(queue)
            if node == destination:
                return distance
            if distance > best[node]:
                continue
            for neighbor, length in self.neighbors(node):
                candidate = distance + length
                if candidate < best.get(neighbor, float("inf")):
                    best[neighbor] = candidate
                    heappush(queue, (candidate, neighbor))
        return float("inf")

    def minutes(self, distance):
        return ceil(distance / self.speed * 60)
