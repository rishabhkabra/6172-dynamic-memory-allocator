echo "
./cache-scratch 1 1000 8 1000000"
./cache-scratch 1 1000 8 1000000
echo "
./cache-scratch 12 1000 8 1000000"
./cache-scratch 12 1000 8 1000000
echo "
./cache-thrash 1 1000 8 1000000"
./cache-thrash 1 1000 8 1000000
echo "
./cache-thrash 12 1000 8 1000000"
./cache-thrash 12 1000 8 1000000
echo "
./larson 10 7 16 1000 10000 6172 12"
./larson 10 7 16 1000 10000 6172 12
echo "
./larson 10 7 8 1000 10000 6172 12"
./larson 10 7 8 1000 10000 6172 12
echo "
./linux-scalability 8 10000000 1"
./linux-scalability 8 10000000 1
echo "
./linux-scalability 8 10000000 12"
./linux-scalability 8 10000000 12
echo "
./linux-scalability 999 10000000 12"
./linux-scalability 999 10000000 12

echo "-----------------------VALIDATION------------------------"
echo "
./validate.py ./cache-scratch-validate 1 1000 8 1000000"
./validate.py ./cache-scratch-validate 1 1000 8 1000000
echo "
./validate.py ./cache-scratch-validate 12 1000 8 1000000"
./validate.py ./cache-scratch-validate 12 1000 8 1000000
echo "
./validate.py ./cache-thrash-validate 1 1000 8 1000000"
./validate.py ./cache-thrash-validate 1 1000 8 1000000
echo "
./validate.py ./cache-thrash-validate 12 1000 8 1000000"
./validate.py ./cache-thrash-validate 12 1000 8 1000000
echo "
./validate.py ./larson-validate 10 7 16 1000 10000 6172 12"
./validate.py ./larson-validate 10 7 16 1000 10000 6172 12
echo "
./validate.py ./larson-validate 10 7 8 1000 10000 6172 12"
./validate.py ./larson-validate 10 7 8 1000 10000 6172 12
echo "
./validate.py ./linux-scalability-validate 8 10000000 1"
./validate.py ./linux-scalability-validate 8 10000000 1
echo "
./validate.py ./linux-scalability-validate 8 10000000 12"
./validate.py ./linux-scalability-validate 8 10000000 12
echo "
./validate.py ./linux-scalability-validate 999 10000000 12"
./validate.py ./linux-scalability-validate 999 10000000 12