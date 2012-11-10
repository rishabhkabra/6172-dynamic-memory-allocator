echo "./cache-thrash 1 5 24 1000000"
./cache-thrash 1 5 24 1000000
echo "
./cache-thrash 6 500 24 1000000"
./cache-thrash 6 500 24 1000000
echo "
./cache-scratch 2 100 8 2000000"
./cache-scratch 2 100 8 2000000
echo "
./cache-scratch 12 100 8 2000000"
./cache-scratch 12 100 8 2000000
echo "
./linux-scalability 12412 16 2"
./linux-scalability 12412 16 2
echo "
./linux-scalability 1241212 16 4"
./linux-scalability 1241212 16 4
echo "
./linux-scalability 1241212 256 10"
./linux-scalability 1241212 256 10
echo "
./larson 10 7 8 1000 10000 43 3"
./larson 10 7 8 1000 10000 43 3
echo "
./larson 8 24 24 10000 4500 1830 6"
./larson 8 24 24 10000 4500 1830 6
echo "
./larson 12 8 80 10000 10000 12731 10"
./larson 12 8 80 10000 10000 12731 10
echo "
./larson 10 1 100 10 10 2343 6"
./larson 10 1 100 10 10 2343 6
echo "
./larson 10 100 100000 10 10 2343 6"
./larson 10 100 100000 10 10 2343 6

echo "
./validate.py ./cache-thrash-validate 100 100 800000 10"
./validate.py ./cache-thrash-validate 100 100 800000 10

echo "
./validate.py ./cache-scratch-validate 6 100 8000000 1"
./validate.py ./cache-scratch-validate 6 100 8000000 1

echo "
./validate.py ./linux-scalability-validate 800000 100 12"
./validate.py ./linux-scalability-validate 800000 100 12
