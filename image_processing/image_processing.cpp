#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <image_filename>\n";
    return -1;
  }

  cv::Mat const input_image = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
  if (input_image.empty()) {
    std::cerr << "Image invalid\n";
    return -1;
  }
  cv::Mat filtered_image{};
  cv::boxFilter(input_image, filtered_image, -1, cv::Size(3, 3),
                cv::Point(-1, -1), true, cv::BORDER_REPLICATE);
  cv::imshow("Original", input_image);
  cv::imshow("Filtered", filtered_image);
  cv::waitKey(0);

  double const standard_deviation = 50.0;
  auto kernel_x = cv::getGaussianKernel(input_image.cols, standard_deviation);
  auto kernel_y = cv::getGaussianKernel(input_image.rows, standard_deviation);
  cv::Mat kernel_x_transpose{};
  cv::transpose(kernel_x, kernel_x_transpose);
  cv::Mat kernel = kernel_y * kernel_x_transpose;
  cv::Mat mask{};

  cv::normalize(kernel, mask, 0, 1, cv::NORM_MINMAX);
  cv::Mat promoted_image{};
  input_image.convertTo(promoted_image, CV_64F);
  cv::multiply(mask, promoted_image, promoted_image);
  cv::convertScaleAbs(promoted_image, promoted_image);
  cv::imshow("Vignette", promoted_image);
  cv::waitKey(0);
  return 0;
}
