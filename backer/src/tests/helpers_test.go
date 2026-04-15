package tests

import (
	"time"

	"github.com/golang-jwt/jwt/v5"
)

// validToken returns a signed JWT with uid and role claims, using testSecret.
func validToken() string {
	token := jwt.NewWithClaims(jwt.SigningMethodHS256, jwt.MapClaims{
		"uid":  "user-1",
		"role": "citizen",
		"exp":  time.Now().Add(time.Hour).Unix(),
	})
	s, _ := token.SignedString([]byte(testSecret))
	return s
}
