transport.pb.go: transport.proto
	protoc transport.proto --go_out=plugins=grpc:. --go_opt=paths=source_relative

force:
	rm -f transport.pb.go
	make transport.pb.go
