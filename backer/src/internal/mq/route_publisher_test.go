package mq

import "testing"

func TestAppendStops_AppendsForSameEmployee(t *testing.T) {
	rp := NewRoutePublisher(nil)

	route1, err := rp.AppendStops("E001", []string{"W001", "W002"})
	if err != nil {
		t.Fatalf("unexpected error on first append: %v", err)
	}
	if len(route1) != 2 || route1[0] != "W001" || route1[1] != "W002" {
		t.Fatalf("unexpected route after first append: %v", route1)
	}

	route2, err := rp.AppendStops("E001", []string{"W003"})
	if err != nil {
		t.Fatalf("unexpected error on second append: %v", err)
	}
	if len(route2) != 3 || route2[0] != "W001" || route2[1] != "W002" || route2[2] != "W003" {
		t.Fatalf("unexpected route after second append: %v", route2)
	}
}

func TestAppendStops_IsolatedByEmployee(t *testing.T) {
	rp := NewRoutePublisher(nil)

	routeA, err := rp.AppendStops("E001", []string{"W001"})
	if err != nil {
		t.Fatalf("unexpected error appending employee A: %v", err)
	}
	if len(routeA) != 1 || routeA[0] != "W001" {
		t.Fatalf("unexpected route for E001: %v", routeA)
	}

	routeB, err := rp.AppendStops("E002", []string{"W010", "W011"})
	if err != nil {
		t.Fatalf("unexpected error appending employee B: %v", err)
	}
	if len(routeB) != 2 || routeB[0] != "W010" || routeB[1] != "W011" {
		t.Fatalf("unexpected route for E002: %v", routeB)
	}

	routeA2, err := rp.AppendStops("E001", []string{"W002"})
	if err != nil {
		t.Fatalf("unexpected error appending employee A second time: %v", err)
	}
	if len(routeA2) != 2 || routeA2[0] != "W001" || routeA2[1] != "W002" {
		t.Fatalf("unexpected route for E001 second append: %v", routeA2)
	}
}

func TestAppendStops_Validation(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, err := rp.AppendStops("", []string{"W001"}); err == nil {
		t.Fatal("expected error for empty employeeID")
	}

	if _, err := rp.AppendStops("E001", []string{}); err == nil {
		t.Fatal("expected error for empty stops")
	}
}

func TestAppendStops_DeduplicatesAgainstExistingRoute(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, err := rp.AppendStops("E001", []string{"W001", "W002"}); err != nil {
		t.Fatalf("unexpected error on first append: %v", err)
	}

	route, err := rp.AppendStops("E001", []string{"W002", "W003", "W001"})
	if err != nil {
		t.Fatalf("unexpected error on dedup append: %v", err)
	}

	// Existing stops are moved to the tail when repeated, then new stops are appended.
	if len(route) != 3 || route[0] != "W002" || route[1] != "W003" || route[2] != "W001" {
		t.Fatalf("unexpected reordered route: %v", route)
	}
}

func TestAppendStops_DuplicateMovesToTail(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, err := rp.AppendStops("E001", []string{"W001", "W002", "W003"}); err != nil {
		t.Fatalf("unexpected append error: %v", err)
	}

	route, err := rp.AppendStops("E001", []string{"W002"})
	if err != nil {
		t.Fatalf("unexpected append error: %v", err)
	}

	if len(route) != 3 || route[0] != "W001" || route[1] != "W003" || route[2] != "W002" {
		t.Fatalf("expected W002 moved to tail, got: %v", route)
	}
}

func TestAppendStops_TrimsAndSkipsEmptyStops(t *testing.T) {
	rp := NewRoutePublisher(nil)

	route, err := rp.AppendStops("E001", []string{"  W001  ", "", "   ", "W002"})
	if err != nil {
		t.Fatalf("unexpected error on trim/skip append: %v", err)
	}

	if len(route) != 2 || route[0] != "W001" || route[1] != "W002" {
		t.Fatalf("unexpected sanitized route: %v", route)
	}
}

func TestRemoveStop_RemovesFirstMatch(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, err := rp.AppendStops("E001", []string{"W001", "W002", "W003"}); err != nil {
		t.Fatalf("unexpected append error: %v", err)
	}

	route, removed, err := rp.RemoveStop("E001", "W002")
	if err != nil {
		t.Fatalf("unexpected remove error: %v", err)
	}
	if !removed {
		t.Fatal("expected stop to be removed")
	}
	if len(route) != 2 || route[0] != "W001" || route[1] != "W003" {
		t.Fatalf("unexpected route after remove: %v", route)
	}
}

func TestRemoveStop_NotFound(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, err := rp.AppendStops("E001", []string{"W001", "W002"}); err != nil {
		t.Fatalf("unexpected append error: %v", err)
	}

	route, removed, err := rp.RemoveStop("E001", "W999")
	if err != nil {
		t.Fatalf("unexpected remove error: %v", err)
	}
	if removed {
		t.Fatal("did not expect removal")
	}
	if len(route) != 2 || route[0] != "W001" || route[1] != "W002" {
		t.Fatalf("unexpected route when stop not found: %v", route)
	}
}

func TestRemoveStop_Validation(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if _, _, err := rp.RemoveStop("", "W001"); err == nil {
		t.Fatal("expected error for empty employeeID")
	}

	if _, _, err := rp.RemoveStop("E001", "   "); err == nil {
		t.Fatal("expected error for empty stop")
	}
}

func TestRegisterAndCompleteShipmentByStop(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if err := rp.RegisterShipmentStops("E001", "SHP1", []string{"W001", "W002"}); err != nil {
		t.Fatalf("unexpected register error: %v", err)
	}

	shipmentID, err := rp.CompleteShipmentByStop("E001", "W001")
	if err != nil {
		t.Fatalf("unexpected complete error: %v", err)
	}
	if shipmentID != "" {
		t.Fatalf("expected no completed shipment yet, got: %q", shipmentID)
	}

	shipmentID, err = rp.CompleteShipmentByStop("E001", "W002")
	if err != nil {
		t.Fatalf("unexpected complete error: %v", err)
	}
	if shipmentID != "SHP1" {
		t.Fatalf("expected SHP1 to complete, got: %q", shipmentID)
	}
}

func TestCompleteShipmentByStop_RespectsQueueOrder(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if err := rp.RegisterShipmentStops("E001", "SHP1", []string{"W001"}); err != nil {
		t.Fatalf("unexpected register error: %v", err)
	}
	if err := rp.RegisterShipmentStops("E001", "SHP2", []string{"W001"}); err != nil {
		t.Fatalf("unexpected register error: %v", err)
	}

	first, err := rp.CompleteShipmentByStop("E001", "W001")
	if err != nil {
		t.Fatalf("unexpected complete error: %v", err)
	}
	if first != "SHP1" {
		t.Fatalf("expected first completion SHP1, got: %q", first)
	}

	second, err := rp.CompleteShipmentByStop("E001", "W001")
	if err != nil {
		t.Fatalf("unexpected complete error: %v", err)
	}
	if second != "SHP2" {
		t.Fatalf("expected second completion SHP2, got: %q", second)
	}
}

func TestRegisterShipmentStops_Validation(t *testing.T) {
	rp := NewRoutePublisher(nil)

	if err := rp.RegisterShipmentStops("", "SHP1", []string{"W001"}); err == nil {
		t.Fatal("expected error for empty employeeID")
	}
	if err := rp.RegisterShipmentStops("E001", "", []string{"W001"}); err == nil {
		t.Fatal("expected error for empty shipmentID")
	}
	if err := rp.RegisterShipmentStops("E001", "SHP1", []string{"   "}); err == nil {
		t.Fatal("expected error for empty stops")
	}
}
