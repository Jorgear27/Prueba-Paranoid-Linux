package worker

import (
	"backer/internal/mq"
	"testing"

	amqp "github.com/rabbitmq/amqp091-go"
)

func TestEmployeeFromRoutingKey(t *testing.T) {
	cases := []struct {
		key  string
		want string
	}{
		{key: "delivered.E001", want: "E001"},
		{key: "delivered. E001 ", want: "E001"},
		{key: "routes.E001", want: ""},
		{key: "delivered.E001.extra", want: ""},
		{key: "", want: ""},
	}

	for _, tc := range cases {
		got := employeeFromRoutingKey(tc.key)
		if got != tc.want {
			t.Fatalf("employeeFromRoutingKey(%q)=%q want %q", tc.key, got, tc.want)
		}
	}
}

func TestHandleDeliveredEvent_RemovesStop(t *testing.T) {
	rp := mq.NewRoutePublisher(nil)
	if _, err := rp.AppendStops("E001", []string{"W001", "W002"}); err != nil {
		t.Fatalf("append failed: %v", err)
	}

	msg := amqp.Delivery{
		RoutingKey: "delivered.E001",
		Body:       []byte(`{"stop":"W001","status":"done"}`),
	}

	handleDeliveredEvent(msg, rp, nil)

	route, removed, err := rp.RemoveStop("E001", "W002")
	if err != nil {
		t.Fatalf("remove failed: %v", err)
	}
	if !removed {
		t.Fatal("expected W002 to still be present after removing W001")
	}
	if len(route) != 0 {
		t.Fatalf("expected empty route, got: %v", route)
	}
}

func TestHandleDeliveredEvent_IgnoresNonDoneStatus(t *testing.T) {
	rp := mq.NewRoutePublisher(nil)
	if _, err := rp.AppendStops("E001", []string{"W001"}); err != nil {
		t.Fatalf("append failed: %v", err)
	}

	msg := amqp.Delivery{
		RoutingKey: "delivered.E001",
		Body:       []byte(`{"stop":"W001","status":"pending"}`),
	}

	handleDeliveredEvent(msg, rp, nil)

	route, removed, err := rp.RemoveStop("E001", "W001")
	if err != nil {
		t.Fatalf("remove failed: %v", err)
	}
	if !removed {
		t.Fatal("expected W001 to remain when status is not done")
	}
	if len(route) != 0 {
		t.Fatalf("expected empty route after explicit remove, got: %v", route)
	}
}
