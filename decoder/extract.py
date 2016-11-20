import csv, os, sys


def extract_logic(file_name):
    csv_reader = None
    try:
        csv_reader = csv.reader(open(file_name,'r'))
    except:
        print("Invalid .csv file")
        exit(1)
    output_file_name = os.path.splitext(file_name)[0] + ".dat"
    output_file = open(output_file_name,'w')
    raw_data = [row for row in csv_reader]
    data_count = 0
    for index in range(len(raw_data)):
        try:
            pair = (float(raw_data[index][0]), int(raw_data[index][1]))
            data_count += 1
            print(pair[1],file=output_file)
        except:
            continue
    print("Output file {} with {} datapoints was created.".format(output_file_name, data_count))


def main():
    name = sys.argv[1] if len(sys.argv) > 2 else input("Enter .csv file name: ")
    extract_logic(name)

if __name__ == "__main__":
    main()

