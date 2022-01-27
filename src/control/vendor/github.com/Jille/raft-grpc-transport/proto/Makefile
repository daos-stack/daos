transport.pb.go: transport.proto
	protoc --go_out=. --go_opt=paths=source_relative --go-grpc_out=. --go-grpc_opt=paths=source_relative transport.proto

force:
	rm -f transport.pb.go
	make transport.pb.go
