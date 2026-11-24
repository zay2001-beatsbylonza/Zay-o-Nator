// ZaY-o-Nator Display - the full-screen VU meter and spectrum for your extra monitor.
// By BeatsbyLonza. MIT licence.
//
// It serves the display page at http://localhost:47910 and opens it in your browser.
// The page listens to an audio input on this computer itself (Loopback, an audio
// interface, a mic). MPCs running the ZaY-o-Nator plugin send their meters here over
// Wi-Fi (UDP port 47911), and the page can show any of them.
package main

import (
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

//go:embed web
var webFiles embed.FS

var build = "dev"

const (
	defaultPort = 47910
	meterPort   = 47911
)

// One meter reading from an MPC (or any sender), as the page receives it.
type frame struct {
	Source string    `json:"source"`
	PeakL  float64   `json:"peakL"` // dBFS
	PeakR  float64   `json:"peakR"`
	RmsL   float64   `json:"rmsL"`
	RmsR   float64   `json:"rmsR"`
	Bands  []float64 `json:"bands"` // dB, bass first
}

type hub struct {
	mu      sync.Mutex
	clients map[chan []byte]struct{}
	seen    map[string]time.Time
}

func newHub() *hub {
	return &hub{clients: map[chan []byte]struct{}{}, seen: map[string]time.Time{}}
}

func (h *hub) publish(msg []byte) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for c := range h.clients {
		select {
		case c <- msg:
		default: // a slow page skips a frame rather than holding everyone up
		}
	}
}

func (h *hub) sources() []string {
	h.mu.Lock()
	defer h.mu.Unlock()
	out := []string{}
	for name, t := range h.seen {
		if time.Since(t) < 5*time.Second {
			out = append(out, name)
		}
	}
	return out
}

// parse reads one packet: "ZAYO1|<name>|peakL|peakR|rmsL|rmsR|b1,b2,...".
func parse(pkt string) (frame, bool) {
	parts := strings.Split(strings.TrimSpace(pkt), "|")
	if len(parts) < 6 || parts[0] != "ZAYO1" {
		return frame{}, false
	}
	name := strings.TrimSpace(parts[1])
	if name == "" || len(name) > 40 {
		return frame{}, false
	}
	f := frame{Source: name}
	vals := []*float64{&f.PeakL, &f.PeakR, &f.RmsL, &f.RmsR}
	for i, p := range vals {
		v, err := strconv.ParseFloat(parts[2+i], 64)
		if err != nil {
			return frame{}, false
		}
		*p = clamp(v, -120, 12)
	}
	if len(parts) > 6 && parts[6] != "" {
		for _, s := range strings.Split(parts[6], ",") {
			v, err := strconv.ParseFloat(s, 64)
			if err != nil || len(f.Bands) >= 128 {
				break
			}
			f.Bands = append(f.Bands, clamp(v, -120, 12))
		}
	}
	return f, true
}

func clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func (h *hub) listenMeters() {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{Port: meterPort})
	if err != nil {
		log.Printf("MPC meters off (port %d busy?): %v", meterPort, err)
		return
	}
	buf := make([]byte, 4096)
	for {
		n, _, err := conn.ReadFromUDP(buf)
		if err != nil {
			continue
		}
		f, ok := parse(string(buf[:n]))
		if !ok {
			continue
		}
		h.mu.Lock()
		h.seen[f.Source] = time.Now()
		h.mu.Unlock()
		msg, _ := json.Marshal(f)
		h.publish(msg)
	}
}

func (h *hub) events(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming not supported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	c := make(chan []byte, 16)
	h.mu.Lock()
	h.clients[c] = struct{}{}
	h.mu.Unlock()
	defer func() {
		h.mu.Lock()
		delete(h.clients, c)
		h.mu.Unlock()
	}()
	tick := time.NewTicker(15 * time.Second)
	defer tick.Stop()
	fmt.Fprint(w, ": hello\n\n")
	fl.Flush()
	for {
		select {
		case <-r.Context().Done():
			return
		case msg := <-c:
			fmt.Fprintf(w, "data: %s\n\n", msg)
			fl.Flush()
		case <-tick.C:
			fmt.Fprint(w, ": keepalive\n\n")
			fl.Flush()
		}
	}
}

func localIP() string {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil {
		return "127.0.0.1"
	}
	defer conn.Close()
	return conn.LocalAddr().(*net.UDPAddr).IP.String()
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("open", url)
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	_ = cmd.Start()
}

func main() {
	port := flag.Int("port", defaultPort, "web page port")
	noOpen := flag.Bool("no-open", false, "don't open the browser")
	flag.Parse()

	h := newHub()
	go h.listenMeters()

	web, _ := fs.Sub(webFiles, "web")
	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.FS(web)))
	mux.HandleFunc("/events", h.events)
	mux.HandleFunc("/api/sources", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(map[string]any{"sources": h.sources(), "build": build})
	})

	// Listen on this computer only: the page uses your audio inputs, so it stays private.
	addr := fmt.Sprintf("127.0.0.1:%d", *port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("Can't start on port %d (is ZaY-o-Nator already open?): %v", *port, err)
	}
	url := fmt.Sprintf("http://localhost:%d", *port)
	fmt.Println("ZaY-o-Nator Display by BeatsbyLonza (build " + build + ")")
	fmt.Println("Open: " + url)
	fmt.Printf("MPCs send meters to %s, port %d\n", localIP(), meterPort)
	fmt.Println("Keep this window open while you use the display.")
	if !*noOpen {
		go func() { time.Sleep(400 * time.Millisecond); openBrowser(url) }()
	}
	log.Fatal(http.Serve(ln, mux))
}
